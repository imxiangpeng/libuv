#include "floor.h"

#define _GNU_SOURCE
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

#include "cjson/cJSON.h"

#define FLOOR_MODEL_VERSION "1.0"
struct floor {
    int num;
    char label[64];  // name
    double height;
    double height_relative;  // height relative to the base floor(ground floor)
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
int floor_load_model(const char* path) {
    int ret = -1;
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

        if (isnan(num) || !label) {
            HR_LOGE("invalid .............\n");
            free(_building.model);
            cJSON_Delete(root);
            return -1;
        }

        p->num = (int)num;
        snprintf(p->label, sizeof(p->label), "%s", label);
        p->height = height;

        if (p->num == base_num) {
            base_id = i;
        }
        HR_LOGD("id:%d, base_id:%d, num:%d name:%s, height:%f\n", i, base_id, p->num, label, height);
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

        HR_LOGD("id:%d, name:%s, height:%f, height_base:%f\n",
                i, _building.model[i].label,
                _building.model[i].height, _building.model[i].height_relative);
    }

    HR_LOGD("=========================\n");

    for (i = 0; i < floors; i++) {
        HR_LOGD("id:%d, name:%s, height:%f, height_base:%f\n",
                i, _building.model[i].label,
                _building.model[i].height, _building.model[i].height_relative);
    }
    cJSON_Delete(root);

    return ret;
}

static int _replace_floor_model_config(const char* path, char* data, int size) {
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

    futil_write_fd(fd, data, size);

    close(fd);

    HR_LOGD("replace %s with :%s\n", path, tmp);
    unlink(path);
    rename(tmp, path);

    free(tmp);
    return 0;
}
static int floor_store_model() {
    int i = 0;
    struct tm tm;
    struct timespec ts;
    char tmp[64] = {0};
    cJSON *root = NULL, *floor_array = NULL;

    clock_gettime(CLOCK_REALTIME, &ts);
    (void)localtime_r(&ts.tv_sec, &tm);

    strftime(tmp, sizeof(tmp) - 1, "%Y.%m.%d %H:%M:%S", &tm);

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
    }

    char* data = cJSON_Print(root);
    HR_LOGD("floor model:%s\n", data);
    _replace_floor_model_config("floor_model_generated.json", data, strlen(data));
    free(data);
    cJSON_Delete(root);
    return 0;
}

static void _observer_on_event(struct motion_event* data) {
    if (!data)
        return;
    if (data->state == STOPPED) {
        double height = data->distance;
        HR_LOGD("%s(%d): runing state changed: height:%f, pressure:%f, _floor_calibration:%d\n", __FUNCTION__, __LINE__, height, data->pressure, _floor_calibration);

        if (_floor_calibration) {
            struct floor* f = &_building.model[_floor_calibration_index];
            f->height = height;
            if (_floor_calibration_index < _building.floors_below_base) {
                f->num = _floor_calibration_index - _building.floors_below_base;
            } else {
                f->num = _floor_calibration_index - _building.floors_below_base + _building.base_floor_num;
            }
            snprintf(f->label, sizeof(f->label), "%d", f->num);
            HR_LOGD("%s(%d): calibration: num:%d, height:%f, index:%d\n", __FUNCTION__, __LINE__, f->num, f->height, _floor_calibration_index);

            if (_floor_calibration_cb) {
                _floor_calibration_cb(_floor_calibration_index, f->num, f->label, f->height);
            }
            _floor_calibration_index++;
            HR_LOGD("%s(%d): calibration: num:%d, height:%f, index:%d, floor_nums:%d\n", __FUNCTION__, __LINE__, f->num, f->height, _floor_calibration_index, _building.floor_nums);
            // we can not detect the last floor
            if (_floor_calibration_index == _building.floor_nums - 1) {
                // process last floor manually
                struct floor* f = &_building.model[_floor_calibration_index];
                f->num = _floor_calibration_index - _building.floors_below_base + _building.base_floor_num;
                snprintf(f->label, sizeof(f->label), "%d", f->num);
                // use previous height as the last floor height
                f->height = height;

                HR_LOGD("%s(%d): floor calibration finished ...\n", __FUNCTION__, __LINE__);
                floor_store_model();

                if (_floor_calibration_cb) {
                    _floor_calibration_cb(_floor_calibration_index, f->num, f->label, f->height);
                    _floor_calibration_cb = NULL;
                }
                _floor_calibration = 0;
            }
        }
    }
}

static struct motion_observer _floor_observer = {
    .on_event = _observer_on_event,
};

int floor_init() {
    floor_load_model("floor_model.json");
    motion_register_observer(&_floor_observer);
    return 0;
}

int floor_deinit() {
    if (_building.model) {
        free(_building.model);
        _building.model = NULL;
    }
    memset((void*)&_building, 0, sizeof(_building));
    return 0;
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
        if (height > f->height_relative - f->height / 2 &&
            height < f->height_relative + f->height / 2) {
            *num = f->num;
            snprintf(label, length, "%s", f->label);
            return 0;
        }
    }

    HR_LOGD("%s(%d): can not found height:%f !!!!!!!!!!!!!!! dundi ........\n", __FUNCTION__, __LINE__, height);
    // dundi

    // exception
    return -1;
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