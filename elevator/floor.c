#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "file_util.h"
#include "hr_log.h"
#include "core.h"

#include "cjson/cJSON.h"

struct floor_model {
    int id;
    char label[64];  // name
    double height;
    double height_base;  // height relative to the base floor
};

static int _floors = 0;
static struct floor_model* _floor_model = NULL;

int floor_load_model(const char* path) {
    int ret = -1;
    ssize_t len = 0;
    char* data = NULL;
    cJSON *root = NULL, *ele = NULL, *floor_array = NULL;
    int floors = 0, id = 0, base_id = -1;
    if (!path) {
        return -1;
    }

    len = futil_read(path, &data);
    if (len <= 0) {
        return -1;
    }

    printf("len:%zd data:%s\n", len, data);
    root = cJSON_ParseWithLength(data, len);
    if (!root) {
        HR_LOGE("can not read file:%s, data:%s\n", path, data);

        HR_LOGE("error:%s\n", cJSON_GetErrorPtr());
        free(data);
        return -1;
    }
    free(data);
    const char* version = cJSON_GetStringValue(cJSON_GetObjectItem(root, "version"));
    const char* date = cJSON_GetStringValue(cJSON_GetObjectItem(root, "date"));
    const char* base = cJSON_GetStringValue(cJSON_GetObjectItem(root, "base"));
    floor_array = cJSON_GetObjectItem(root, "floor");
    floors = cJSON_GetArraySize(floor_array);
    printf("version: %s, date:%s, base:%s, floors:%d\n", version, date, base, floors);
    if (!version || !date || !base) {
        cJSON_Delete(root);
        return -1;
    }
    printf("version: %s, date:%s, base:%s, floors:%d\n", version, date, base, floors);
    if (floors > 0) {
        _floor_model = (struct floor_model*)calloc(sizeof(struct floor_model), floors);
        if (!_floor_model) {
            cJSON_Delete(root);
        }
    }

    _floors = floors;

    id = 0;
    cJSON_ArrayForEach(ele, floor_array) {
        struct floor_model* p = &_floor_model[id];
        // int id = (int)cJSON_GetNumberValue(cJSON_GetObjectItem(ele, "id"));
        const char* label = cJSON_GetStringValue(cJSON_GetObjectItem(ele, "name"));
        double height = cJSON_GetNumberValue(cJSON_GetObjectItem(ele, "height"));
        printf("id:%d, name:%s, height:%f\n", id, label, height);
        // p->id = id;
        snprintf(p->label, sizeof(p->label), "%s", label);
        p->height = height;

        if (strcmp(p->label, base) == 0) {
            base_id = id;
        }
        id++;
    }

    // 1 lou height == 0
    _floor_model[base_id].height_base = 0;
    for (id = base_id - 1; id >= 0; id--) {
        _floor_model[id].height_base = _floor_model[id + 1].height_base - _floor_model[id].height;

        printf("id:%d, name:%s, height:%f, height_base:%f\n",
               id, _floor_model[id].label,
               _floor_model[id].height, _floor_model[id].height_base);
    }
    
    
    for (id = base_id + 1; id < floors; id++) {
        _floor_model[id].height_base = _floor_model[id - 1].height + _floor_model[id - 1].height_base;

        printf("id:%d, name:%s, height:%f, height_base:%f\n",
               id, _floor_model[id].label,
               _floor_model[id].height, _floor_model[id].height_base);
    }
    
    printf("=========================\n");

    for (id  = 0; id < floors; id++) {
        
        printf("id:%d, name:%s, height:%f, height_base:%f\n",
               id, _floor_model[id].label,
               _floor_model[id].height, _floor_model[id].height_base);
    }
    cJSON_Delete(root);

    return ret;
}
static void _observer_update(enum core_sensor sensor, void *data) {
    struct live_stat *stat = (struct live_stat *)data;

    //printf("speed:%f\n", stat->speed);
    //printf("distance:%f\n", stat->distance);
    //printf("height:%f\n", stat->height);
    int i = 0; 
    int min = 0;
    int max = 0;
    for (i = 0; i < _floors; i++) {
        if (stat->height > _floor_model[i].height_base - _floor_model[i].height / 2 &&
        stat->height < _floor_model[i].height_base + _floor_model[i].height / 2 ) {
            printf("height: %f ==> %s\n", stat->height, _floor_model[i].label);
        }
    }
}


static struct core_observer _floor_core_observer = {
    .update = _observer_update};


int floor_init() {
    floor_load_model("floor_model.json");
    core_register_observer(SENSOR_ACCELERATION, &_floor_core_observer);
}