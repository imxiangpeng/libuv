#include "floor.h"

#define _GNU_SOURCE
#define _XOPEN_SOURCE 600

#include <fcntl.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "file_util.h"
#include "hr_log.h"
#include "motion.h"
#include "time_utils.h"

#include "cjson/cJSON.h"

#define FLOOR_MODEL_VERSION "1.0"

enum {
    STORE_PERSIST = 1,
    STORE_PERSIST_BACKUP = 1 << 1,
    STORE_TMPFS = 1 << 2,
};

// this model is generated when user trigger floor calibration
// we will not use pressure in this model
// because pressure maybe update frequently
#define FLOOR_MODEL_PATH "floor_model.json"  //"/etc/elevatord/floor_model.json"
#define FLOOR_MODEL_BACKUP_PATH "/etc/elevatord/floor_model.1.json"
#define FLOOR_MODEL_TMPFS_PATH "/tmp/elevatord_floor_model.json"  //"/etc/elevatord/floor_model.json"

#define FLOOR_PREDICT_PRESSURE_DELTA 30    // 30Pa
#define FLOOR_PRESSURE_THRESHOLD_DELTA 10  // 10Pa

#define FLOOR_MODEL_HISTORY_COUNT 2

static const double PRESSURE_L = 0.0065;
static const double PRESSURE_R = 8.31432;
static const double PRESSURE_M = 0.0289644;

static const double G0 = 9.81;
struct floor {
    int num;
    char label[64];  // name
    double height;
    double height_relative;  // height relative to the base floor(ground floor)
    double pressure;
    double temperature;
};

struct building_model {
    int base_floor_num;
    int floors_below_base;
    int floor_nums;
    struct floor* model;
} _building = {0, 0, 0, NULL};

// only support one caller
static int _floor_calibration = 0;
static int _floor_calibration_index = 0;

static floor_calibration_cb _floor_calibration_cb = NULL;

static double _uncommit_pressure_delta = 0;

static int floor_load_model(const char* path) {
    ssize_t len = 0;
    char *data = NULL, *version = NULL, *date = NULL;
    cJSON *root = NULL, *ele = NULL, *floor_array = NULL;
    int floors = 0, i = 0, base_id = -1;
    double base_num = 1;

    if (!path) {
        return -1;
    }

    len = futil_read(path, &data);
    if (len <= 0) {
        return -1;
    }

    root = cJSON_ParseWithLength(data, len);
    free(data);

    if (!root) {
        HR_LOGE("can not read file:%s, data:%s\n", path, data);

        HR_LOGE("error:%s\n", cJSON_GetErrorPtr());
        return -1;
    }

    version = cJSON_GetStringValue(cJSON_GetObjectItem(root, "version"));
    date = cJSON_GetStringValue(cJSON_GetObjectItem(root, "date"));
    base_num = cJSON_GetNumberValue(cJSON_GetObjectItem(root, "base_num"));
    floor_array = cJSON_GetObjectItem(root, "floor");
    floors = cJSON_GetArraySize(floor_array);

    if (!version || !date || isnan(base_num) || floors == 0) {
        cJSON_Delete(root);
        return -1;
    }

    HR_LOGD("version: %s, date:%s, base:%f, floors:%d\n", version, date, base_num, floors);

    _building.base_floor_num = base_num;
    if (!_building.model || _building.floor_nums != floors) {
        if (_building.model) {
            free(_building.model);
            _building.floor_nums = 0;
        }
        _building.model = (struct floor*)calloc(sizeof(struct floor), floors);
        if (!_building.model) {
            cJSON_Delete(root);
            return -1;
        }
        _building.floor_nums = floors;
    }

    i = 0;
    cJSON_ArrayForEach(ele, floor_array) {
        struct floor* p = &_building.model[i];
        double num = cJSON_GetNumberValue(cJSON_GetObjectItem(ele, "num"));
        const char* label = cJSON_GetStringValue(cJSON_GetObjectItem(ele, "name"));
        double height = cJSON_GetNumberValue(cJSON_GetObjectItem(ele, "height"));
        double pressure = cJSON_GetNumberValue(cJSON_GetObjectItem(ele, "pressure"));

        if (isnan(num) || !label) {
            HR_LOGE("invalid .............\n");
            free(_building.model);
            cJSON_Delete(root);
            return -1;
        }

        p->num = (int)num;
        snprintf(p->label, sizeof(p->label), "%s", label);
        p->height = height;

        if (isnan(pressure)) {
            p->pressure = 0;
        } else {
            p->pressure = pressure;
        }

        if (p->num == base_num) {
            base_id = i;
        }
        HR_LOGD("id:%d, base_id:%d, num:%d name:%s, height:%f, pressure:%f\n", i, base_id, p->num, label, height, pressure);
        i++;
    }

    // 1 lou height == 0
    _building.model[base_id].height_relative = 0;
    for (i = base_id - 1; i >= 0; i--) {
        _building.model[i].height_relative = _building.model[i + 1].height_relative - _building.model[i].height;

        HR_LOGD("id:%d, name:%s, height:%f, height_base:%f\n",
                i, _building.model[i].label,
                _building.model[i].height, _building.model[i].height_relative);
    }

    for (i = base_id + 1; i < floors; i++) {
        _building.model[i].height_relative = _building.model[i - 1].height + _building.model[i - 1].height_relative;
    }

    HR_LOGD("========================= Floor Model Begin =========================\n");

    for (i = 0; i < floors; i++) {
        HR_LOGD("id:%d, name:%s, height:%f, height_base:%f, pressure:%f\n",
                i, _building.model[i].label,
                _building.model[i].height, _building.model[i].height_relative, _building.model[i].pressure);
    }
    HR_LOGD("========================= Floor Model End =========================\n");

    cJSON_Delete(root);

    _uncommit_pressure_delta = 0;
    return 0;
}

static int _replace_floor_model_config(const char* path, const char* data, int size) {
    int fd = -1;
    char* tmp = NULL;
    int tmp_len = 0;

    const char* TMPFILE_TEMPLATE = "tmp_XXXXXX";

    tmp_len = strlen(path) + strlen(TMPFILE_TEMPLATE) + 1;  // + '\0'

    tmp = (char*)calloc(1, tmp_len);  // hardcode 8(.XXXXXX + \0)
    if (!tmp)
        return -1;

    snprintf(tmp, tmp_len, "%s%s", path, TMPFILE_TEMPLATE);
    fd = mkostemp(tmp, O_RDWR | O_TRUNC | O_CREAT);
    if (fd < 0) {
        free(tmp);
        return -1;
    }

    fchmod(fd, S_IRUSR | S_IWUSR | S_IRGRP);

    futil_write_fd(fd, (char*)data, size);

    close(fd);

    HR_LOGD("replace %s with :%s\n", path, tmp);
    unlink(path);
    rename(tmp, path);

    free(tmp);
    return 0;
}

static int floor_store_model(int mode) {
    int i = 0;
    struct tm tm;
    struct timespec ts;
    char tmp[64] = {0};
    cJSON *root = NULL, *floor_array = NULL;

    if (!_building.model) {
        return -1;
    }

    clock_gettime(CLOCK_REALTIME, &ts);
    (void)localtime_r(&ts.tv_sec, &tm);

    strftime(tmp, sizeof(tmp) - 1, "%Y-%m-%d %H:%M:%S", &tm);

    root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "version", FLOOR_MODEL_VERSION);
    cJSON_AddStringToObject(root, "date", tmp);
    cJSON_AddNumberToObject(root, "base_num", _building.base_floor_num);

    floor_array = cJSON_AddArrayToObject(root, "floor");
    if (!floor_array) {
        cJSON_Delete(root);
        return -1;
    }

    for (i = 0; i < _building.floor_nums; i++) {
        struct floor* f = &_building.model[i];
        cJSON* ele = cJSON_CreateObject();
        if (!ele) {
            cJSON_Delete(root);
            return -1;
        }
        cJSON_AddItemToArray(floor_array, ele);
        cJSON_AddNumberToObject(ele, "num", (double)f->num);
        snprintf(tmp, sizeof(tmp), "%d", f->num);
        cJSON_AddStringToObject(ele, "name", tmp);
        cJSON_AddNumberToObject(ele, "height", f->height);
        cJSON_AddNumberToObject(ele, "pressure", f->pressure);
    }

    char* data = cJSON_Print(root);
    HR_LOGD("floor model:%s\n", data);

    if (STORE_PERSIST_BACKUP & mode) {
        _replace_floor_model_config(FLOOR_MODEL_BACKUP_PATH, data, strlen(data));
    }

    if (STORE_PERSIST & mode) {
        _replace_floor_model_config(FLOOR_MODEL_PATH, data, strlen(data));
    }
    if (STORE_TMPFS & mode) {
        _replace_floor_model_config(FLOOR_MODEL_TMPFS_PATH, data, strlen(data));
    }

    free(data);
    cJSON_Delete(root);
    return 0;
}

static void _observer_on_event(struct motion_event* data) {
    if (!data)
        return;

    // start running, record current pressure
    // 但是开始运行气压值要比实际小一些，因为关门时气压会下降 7 - 10 Pa
    // 我们还是希望后续通过停止时的气压值作为当前楼层气压值
    if (data->state == ACCELERATING) {
        if (_floor_calibration) {
            struct floor* f = &_building.model[_floor_calibration_index];
            f->pressure = data->pressure;
        }
        return;
    }

    if (data->state == STOPPED) {
        double height = data->distance;
        HR_LOGD("%s(%d): runing state changed: height:%f, pressure:%f, _floor_calibration:%d\n", __FUNCTION__, __LINE__, height, data->pressure, _floor_calibration);

        if (_floor_calibration) {
            struct floor* f = &_building.model[_floor_calibration_index];
            f->height = height;
            // do not assign pressure, it should be assigned when start running
            // current pressure is current floor's value
            // however this floor is previous
            // f->pressure = data->pressure;
            if (_floor_calibration_index < _building.floors_below_base) {
                f->num = _floor_calibration_index - _building.floors_below_base;
            } else {
                f->num = _floor_calibration_index - _building.floors_below_base + _building.base_floor_num;
            }
            snprintf(f->label, sizeof(f->label), "%d", f->num);

            HR_LOGD("%s(%d): calibration: num:%d, height:%f, pressure:%f, index:%d\n", __FUNCTION__, __LINE__, f->num, f->height, f->pressure, _floor_calibration_index);

            if (_floor_calibration_cb) {
                _floor_calibration_cb(_floor_calibration_index, f->num, f->label, f->height, f->pressure, 0);
            }
            _floor_calibration_index++;
            HR_LOGD("%s(%d): calibration: num:%d, height:%f, index:%d, floor_nums:%d\n", __FUNCTION__, __LINE__, f->num, f->height, _floor_calibration_index, _building.floor_nums);
            // we can not detect the last floor
            if (_floor_calibration_index == _building.floor_nums - 1) {
                // process last floor manually
                int num = 0;
                char label[256] = {0};
                double pressure = 0;
                struct floor* f = &_building.model[_floor_calibration_index];
                f->num = _floor_calibration_index - _building.floors_below_base + _building.base_floor_num;
                snprintf(f->label, sizeof(f->label), "%d", f->num);
                // use previous height as the last floor height
                f->height = height;
                // this is the current floor pressure
                f->pressure = data->pressure;

                num = f->num;
                snprintf(label, sizeof(label), "%s", f->label);
                pressure = f->pressure;

                HR_LOGD("%s(%d): floor calibration finished ...\n", __FUNCTION__, __LINE__);

                // store model file before last calibration completed event
                // so they can read model data
                floor_store_model(STORE_PERSIST | STORE_PERSIST_BACKUP);  // update backup when calibration
                // reload or calc relative height
                floor_load_model(FLOOR_MODEL_PATH);

                if (_floor_calibration_cb) {
                    _floor_calibration_cb(_floor_calibration_index, num, label, height, pressure, 1 /*completed*/);
                    _floor_calibration_cb = NULL;
                }
                _floor_calibration = 0;

                // finally we should update accelerometer height
                // because now it's related from the lowest floor, not base floor
                // it's not correct when lowest floor is not base floor
                motion_calibrate_at_floor(num);
            }
        }
    }
}

static struct motion_observer _floor_observer = {
    .on_event = _observer_on_event,
};

int floor_init() {
    int ret = -1;
    struct stat st;

    // load from tmpfs when it's exist
    if (lstat(FLOOR_MODEL_TMPFS_PATH, &st) == 0) {
        ret = floor_load_model(FLOOR_MODEL_TMPFS_PATH);
    }
    if (ret != 0) {
        floor_load_model(FLOOR_MODEL_PATH);
    }

    motion_register_observer(&_floor_observer);
    return 0;
}

int floor_deinit() {
    // write floor model
    floor_store_model(STORE_PERSIST | STORE_TMPFS);
    if (_building.model) {
        free(_building.model);
        _building.model = NULL;
    }
    memset((void*)&_building, 0, sizeof(_building));
    return 0;
}

int floor_base_floor(void) {
    return _building.base_floor_num;
}

const char* floor_model_data_path(void) {
    return FLOOR_MODEL_PATH;
}

// return predict floor according height
int floor_predict(double height, int* num, char* label, int length) {
    int i = 0;

    if (!num || !label) {
        return -1;
    }

    // not support when calibration
    if (_floor_calibration) {
        return -1;
    }

    for (i = 0; i < _building.floor_nums; i++) {
        struct floor* f = &_building.model[i];
        // HR_LOGD("%s(%d): height: %f, floor:%d, [%f,%f]\n", __FUNCTION__, __LINE__, height, f->num, f->height_relative - f->height / 2, f->height_relative + f->height / 2);
        // ignore! there may be gaps, especially when the floor heights are different.
        // not very good!
        if (height > f->height_relative - f->height / 2 &&
            height < f->height_relative + f->height / 2) {
            *num = f->num;
            snprintf(label, length, "%s", f->label);
            return 0;
        }
    }

    // dundi detect!

    // exception
    return -1;
}

// must called when stopped
int floor_predict_with_pressure(double pressure, double* height, int* num, char* label, int length) {
    int i = 0;

    if (!height || !num || !label) {
        return -1;
    }

    // not support when calibration
    if (_floor_calibration) {
        return -1;
    }

    for (i = 0; i < _building.floor_nums; i++) {
        struct floor* f = &_building.model[i];
        double delta_p = fabs(pressure - _building.model[i].pressure);
        // HR_LOGD("%s(%d): pressure:%f with floor:%d -> delta:%f\n", __FUNCTION__, __LINE__, pressure, f->num, f->pressure - pressure);
        if (delta_p < FLOOR_PREDICT_PRESSURE_DELTA) {
            // should verify next floor
            if (i < _building.floor_nums - 1) {
                double delta_p2 = fabs(pressure - _building.model[i + 1].pressure);
                if (delta_p > delta_p2) {
                    f = &_building.model[i + 1];
                }
            }
            *num = f->num;
            *height = f->height_relative;
            snprintf(label, length, "%s", f->label);
            HR_LOGD("%s(%d): matched pressure:%f with floor:%d -> delta:%f\n", __FUNCTION__, __LINE__, pressure, f->num, f->pressure - pressure);
            return 0;
        }
    }

    // dundi

    // exception
    return -1;
}

static double calculate_height_difference(double p0, double p1, double temperature) {
    static double fac = PRESSURE_L * PRESSURE_R / PRESSURE_M / G0;
    return ((temperature + 273.15) / PRESSURE_L) * (1 - pow(p1 / p0, fac /*0.190284*/));
}

static double calculate_base_pressure(double p1, double height, double temperature) {
    double fac = (PRESSURE_L * PRESSURE_R) / (PRESSURE_M * G0);
    double T_kelvin = temperature + 273.15;
    double ratio = 1.0 - (PRESSURE_L * height) / T_kelvin;

    if (ratio <= 0.0) {
        return -1.0;  // invalid
    }

    double exponent = 1.0 / fac;
    double p0 = p1 / pow(ratio, exponent);
    return p0;
}

// int find_closest_ordered(double arr[], int size, double target) {
//     int closest_index = 0;
//     double min_diff = fabs(arr[0] - target);

//     for (int i = 1; i < size; i++) {
//         double diff = fabs(arr[i] - target);

//         // 如果当前差值更小，则更新
//         if (diff < min_diff) {
//             min_diff = diff;
//             closest_index = i;
//         } else {
//             // 差值开始增大，提前结束
//             break;
//         }
//     }

//     return closest_index;
// }
// force update floor_module.json when persist is not 0
// otherwise update memory data only
int floor_update_pressure_when_stationary(int num, double pressure, double temperature, int persist) {
    (void)num;
    (void)pressure;
    (void)temperature;
    (void)persist;
    struct floor* fb = NULL;

    double delta_p = 0;
    int mode = STORE_TMPFS;

    // not support when calibration
    if (_floor_calibration) {
        return -1;
    }

    for (int i = 0; i < _building.floor_nums; i++) {
        if (_building.model[i].num == num) {
            fb = &_building.model[i];
            break;
        }
    }

    if (!fb) {
        // no such floor
        return -1;
    }

    delta_p = pressure - fb->pressure;
    _uncommit_pressure_delta += delta_p;

    HR_LOGD("%s(%d): floor:%d, store pressure:%f, new :%f (delta:%f), uncommit pressure delta:%f\n", __FUNCTION__, __LINE__, num, fb->pressure, pressure, pressure - fb->pressure, _uncommit_pressure_delta);

#if 0
    if (persist != 0) {
        if (fabs(_uncommit_pressure_delta /*delta_p*/) < FLOOR_PRESSURE_THRESHOLD_DELTA) {
            return 0;  // no need update
        }
    }
#endif

    fb->pressure = pressure;
    fb->temperature = temperature;

    for (int i = 0; i < _building.floor_nums; i++) {
        struct floor* fr = &_building.model[i];
        if (fb != fr) {
            double p0 = calculate_base_pressure(pressure, fb->height_relative - fr->height_relative, temperature);
            if (p0 > 0) {
                p0 = round(p0 * 100) / 100;
                HR_LOGD("%s(%d): update floor:%d, pressure %f -> %f, temperature: %f -> %f, height:%f\n", __FUNCTION__, __LINE__,
                        fr->num,
                        fr->pressure, p0, fr->temperature, temperature, calculate_height_difference(p0, pressure, temperature));
                fr->pressure = p0;
                fr->temperature = temperature;
            }
        }
    }

#if 0
    if (persist != 0) {
        _uncommit_pressure_delta = 0;
        floor_store_model(STORE_PERSIST);
    } else {
        floor_store_model(STORE_TMPFS);

        // also force update persist
        if (fabs(_uncommit_pressure_delta) >= FLOOR_PRESSURE_THRESHOLD_DELTA){
            _uncommit_pressure_delta = 0;
            floor_store_model(STORE_PERSIST);
        }
    }
#endif
    if (fabs(_uncommit_pressure_delta) >= FLOOR_PRESSURE_THRESHOLD_DELTA) {
        _uncommit_pressure_delta = 0;
        mode |= STORE_PERSIST;
    }

    floor_store_model(mode);

    return 0;
}
// height relative to base floor
int floor_relative_height(int num, double* height) {
    int i = 0;
    if (!height) {
        return -1;
    }

    // not support when calibration
    if (_floor_calibration) {
        return -1;
    }

    for (i = 0; i < _building.floor_nums; i++) {
        struct floor* f = &_building.model[i];
        if (f->num == num) {
            *height = f->height_relative;
            return 0;
        }
    }

    return -1;
}

// base_floor: base floor number
// floors_below_base: number of floors below the base floor.
// floors_above_base: number of floors above the base floor.
// 通常基层可以选择 1 楼，总楼层就是 地下层数 + 地上层数（含 1 楼）
int floor_enter_calibration(int base_floor, int floors_below_base, int floors_above_base) {
    int floors_max = floors_below_base + floors_above_base;

    if (_floor_calibration == 1) {
        HR_LOGE("it's in floor calibration, please wait finished ...\n");
        return -1;
    }

    _floor_calibration = 1;
    _floor_calibration_index = 0;

    if (_building.model && _building.floor_nums != floors_max) {
        free(_building.model);
        _building.model = NULL;
        _building.floor_nums = 0;
    }

    if (!_building.model) {
        _building.model = (struct floor*)calloc(sizeof(struct floor), floors_max);
        if (!_building.model) {
            return -1;
        }
        _building.floor_nums = floors_max;
    }

    memset((void*)_building.model, 0, sizeof(struct floor) * _building.floor_nums);

    // initialize base floor
    _building.base_floor_num = base_floor;
    _building.floors_below_base = floors_below_base;

    struct floor* bf = &_building.model[floors_below_base];
    bf->height_relative = 0;
    bf->num = base_floor;
    snprintf(bf->label, sizeof(bf->label), "%d", base_floor);

    return 0;
}

int floor_enter_calibration_with_callback(int base_floor, int floors_below_base, int floors_above_base, floor_calibration_cb cb) {
    if (0 != floor_enter_calibration(base_floor, floors_below_base, floors_above_base)) {
        return -1;
    }
    _floor_calibration_cb = cb;
    return 0;
}

// verify data format and write to persist floor model
// reload at last!
int floor_update_floor_model_data(const char* data) {
    char *version = NULL, *date = NULL;
    cJSON *root = NULL, *ele = NULL, *floor_array = NULL;
    int floors = 0;
    double base_num = 1;
    if (!data) {
        return -1;
    }

    root = cJSON_Parse(data);

    if (!root) {
        HR_LOGE("error:%s\n", cJSON_GetErrorPtr());
        return -1;
    }

    version = cJSON_GetStringValue(cJSON_GetObjectItem(root, "version"));
    date = cJSON_GetStringValue(cJSON_GetObjectItem(root, "date"));
    base_num = cJSON_GetNumberValue(cJSON_GetObjectItem(root, "base_num"));
    floor_array = cJSON_GetObjectItem(root, "floor");
    floors = cJSON_GetArraySize(floor_array);

    if (!version || !date || isnan(base_num) || floors == 0) {
        cJSON_Delete(root);
        return -1;
    }

    HR_LOGD("version: %s, date:%s, base:%f, floors:%d\n", version, date, base_num, floors);

    cJSON_ArrayForEach(ele, floor_array) {
        double num = cJSON_GetNumberValue(cJSON_GetObjectItem(ele, "num"));
        const char* label = cJSON_GetStringValue(cJSON_GetObjectItem(ele, "name"));
        double height = cJSON_GetNumberValue(cJSON_GetObjectItem(ele, "height"));
        // ignore pressure
        // double pressure = cJSON_GetNumberValue(cJSON_GetObjectItem(ele, "pressure"));

        if (isnan(num) || !label || isnan(height)) {
            HR_LOGE("invalid .............\n");
            cJSON_Delete(root);
            return -1;
        }
    }

    _replace_floor_model_config(FLOOR_MODEL_PATH, data, strlen(data));
    // store model file before last calibration completed event
    // so they can read model data
    // floor_store_model(STORE_PERSIST /*| STORE_PERSIST_BACKUP*/);  // update backup when calibration
    // reload or calc relative height
    floor_load_model(FLOOR_MODEL_PATH);

    return 0;
}