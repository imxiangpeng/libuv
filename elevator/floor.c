#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "core.h"
#include "file_util.h"
#include "hr_log.h"

#include "cjson/cJSON.h"

struct floor {
    int num;
    char label[64];  // name
    double height;
    double height_relative;  // height relative to the base floor
};

// static int _building.models = 0;
// static struct floor* _building.model = NULL;

struct building_model {
    int base_floor_num;
    int floor_nums;
    struct floor* model;
} _building = {0, 0, NULL};

int floor_load_model(const char* path) {
    int ret = -1;
    ssize_t len = 0;
    char* data = NULL, *version = NULL, *date = NULL;
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

    printf("version: %s, date:%s, base:%f, floors:%d\n", version, date, base_num, floors);

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

        if (isnan(num) || !label || !height) {
            printf("invalid .............\n");

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
        printf("id:%d, base_id:%d, num:%d name:%s, height:%f\n", i, base_id, p->num, label, height);
        i++;
    }

    // 1 lou height == 0
    _building.model[base_id].height_relative = 0;
    for (i = base_id - 1; i >= 0; i--) {
        _building.model[i].height_relative = _building.model[i + 1].height_relative - _building.model[i].height;

        printf("id:%d, name:%s, height:%f, height_base:%f\n",
               i, _building.model[i].label,
               _building.model[i].height, _building.model[i].height_relative);
    }

    for (i = base_id + 1; i < floors; i++) {
        _building.model[i].height_relative = _building.model[i - 1].height + _building.model[i - 1].height_relative;

        printf("id:%d, name:%s, height:%f, height_base:%f\n",
               i, _building.model[i].label,
               _building.model[i].height, _building.model[i].height_relative);
    }

    printf("=========================\n");

    for (i = 0; i < floors; i++) {
        printf("id:%d, name:%s, height:%f, height_base:%f\n",
               i, _building.model[i].label,
               _building.model[i].height, _building.model[i].height_relative);
    }
    cJSON_Delete(root);

    return ret;
}

int floor_init() {
    floor_load_model("floor_model.json");
}

// return predict floor according height
int floor_predict(double height, int* num, char* label, int length) {
    int i = 0;
    for (i = 0; i < _building.floor_nums; i++) {
        struct floor* f = &_building.model[i];
        if (height > f->height_relative - f->height / 2 &&
            height < f->height_relative + f->height / 2) {
            *num = f->num;
            snprintf(label, length, "%s", f->label);
            return 0;
        }
    }

    // exception
    return -1;
}

double floor_height(int num) {
    int i = 0;
    for (i = 0; i < _building.floor_nums; i++) {
        struct floor* f = &_building.model[i];
        if (f->num == num)
            return f->height_relative;
    }
}