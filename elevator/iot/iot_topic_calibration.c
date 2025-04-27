
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "iot_topic.h"
/// publish every 10s
#include "cjson/cJSON.h"
#include "floor.h"
#include "hr_list.h"
#include "hr_log.h"
#include "motion.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define SVC_METHOD_START_AUTO_FLOOR_CALIBRATION "thing.service.StartAutoFloorCalibration"
#define SVC_METHOD_CALIBRATE_AT_FLOOR_MANUALLY "thing.service.CalibrateAtFloorManually"
#define SVC_METHOD_CALIBRATE_AT_HEIGHT_MANUALLY "thing.service.CalibrateAtHeightManually"

enum {
    CALIBRATION_TOPIC_AUTO_FLOOR_CALIBRATION_EVENT = 0,
    CALIBRATION_TOPIC_START_AUTO_FLOOR_CALIBRATION,
    CALIBRATION_TOPIC_CALIBRATE_AT_FLOOR_MANUALLY,
    CALIBRATION_TOPIC_CALIBRATE_AT_HEIGHT_MANUALLY,
    _CALIBRATION_TOPIC_MAX
};

struct svc_action {
    const char* name;
    int (*method)(cJSON* param);
};

struct calibration_event {
    int id;
    int floor;
    char label[64];
    double height;
    struct hr_list_head entry;
};

static HR_LIST_HEAD(_auto_floor_calibration_message_queue);

static struct iot_topic _iot_calibration_topics[];

static int _floor_calibration = 0;
static int _floor_calibration_index = 0;
static int _floors_below_base = 0;
static int _floors_above_base = 0;
static int _floor_base = 1;

static void _iot_floor_calibration_observer_on_event(struct motion_event* data);
static int _StartAutoFloorCalibration(cJSON* params);
static int _CalibrateAtFloorManually(cJSON* params);
static int _CalibrateAtHeightManually(cJSON* params);
static struct svc_action _svc_action_tbl[] = {
    {SVC_METHOD_START_AUTO_FLOOR_CALIBRATION, _StartAutoFloorCalibration},
    {SVC_METHOD_CALIBRATE_AT_FLOOR_MANUALLY, _CalibrateAtFloorManually},
    {SVC_METHOD_CALIBRATE_AT_HEIGHT_MANUALLY, _CalibrateAtHeightManually},
    {NULL, NULL},  // keep it
};

static struct motion_observer _calibration_observer = {
    .on_event = _iot_floor_calibration_observer_on_event,
};

static struct calibration_event* calibration_event_alloc() {
    struct calibration_event* e = (struct calibration_event*)calloc(1, sizeof(struct calibration_event));
    if (!e) {
        return NULL;
    }

    HR_INIT_LIST_HEAD(&e->entry);

    return e;
}

static void calibration_event_free(struct calibration_event* e) {
    if (!e) {
        return;
    }

    hr_list_del(&e->entry);

    HR_INIT_LIST_HEAD(&e->entry);
}

static int send_calibration_event(struct calibration_event* m) {
    hr_list_add_tail(&m->entry, &_auto_floor_calibration_message_queue);
    return iot_topic_public_async(&_iot_calibration_topics[CALIBRATION_TOPIC_AUTO_FLOOR_CALIBRATION_EVENT]);
}

static int _on_auto_floor_calibration_event_publish(void** payload, int* len) {
    char tmp[256] = {0};
    struct calibration_event* e = NULL;
    cJSON *root = NULL, *param = NULL;

    // not new message, no need publish
    if (hr_list_empty(&_auto_floor_calibration_message_queue)) {
        return -1;
    }

    e = hr_list_first_entry(&_auto_floor_calibration_message_queue, struct calibration_event, entry);

    root = cJSON_CreateObject();
    if (!root) {
        calibration_event_free(e);
        return -1;
    }

    snprintf(tmp, sizeof(tmp), "%d", iot_generate_mid());
    cJSON_AddStringToObject(root, "id", tmp);
    cJSON_AddStringToObject(root, "version", "1.0");

    param = cJSON_AddObjectToObject(root, "params");
    cJSON_AddNumberToObject(param, "Id", e->id);
    cJSON_AddNumberToObject(param, "Floor", e->floor);
    cJSON_AddStringToObject(param, "Label", e->label);
    cJSON_AddNumberToObject(param, "Height", e->height);

    // free the message
    calibration_event_free(e);

    *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!*payload)
        return -1;

    *len = strlen(*payload);
    HR_LOGD("publish: %s\n", *payload);
    
    if (!hr_list_empty(&_auto_floor_calibration_message_queue)) {
        // when queue is not empty, we should trigger again
        // because uv_async merges multiple requests and triggers the callback only once
        return iot_topic_public_async(&_iot_calibration_topics[CALIBRATION_TOPIC_AUTO_FLOOR_CALIBRATION_EVENT]);
    }
    return 0;
}

static int _on_reply_message(void* payload, int len) {
    printf("reply message %d -> %s\n", len, (char*)payload);
    return 0;
}

static void _iot_floor_calibration_observer_on_event(struct motion_event* data) {
    if (!data)
        return;
    if (data->state == STOPPED) {
        double height = data->distance;
        HR_LOGD("%s(%d): runing state changed: height:%f, pressure:%f, _floor_calibration:%d\n", __FUNCTION__, __LINE__, height, data->pressure, _floor_calibration);

        if (_floor_calibration) {
            struct calibration_event* e = calibration_event_alloc();
            e->id = _floor_calibration_index;
            e->height = height;
            if (_floor_calibration_index < _floors_below_base) {
                e->floor = _floor_calibration_index - _floors_below_base;
            } else {
                e->floor = _floor_calibration_index - _floors_below_base + _floor_base;
            }
            snprintf(e->label, sizeof(e->label), "%d", e->floor);
            HR_LOGD("%s(%d): calibration: num:%d, height:%f, index:%d\n", __FUNCTION__, __LINE__, e->floor, e->height, _floor_calibration_index);

            send_calibration_event(e);

            _floor_calibration_index++;
            // we can not detect the last floor
            if (_floor_calibration_index == _floors_below_base + _floors_above_base - 1) {
                // process last floor manually
                //
                struct calibration_event* e = calibration_event_alloc();
                e->id = _floor_calibration_index;
                e->floor = _floor_calibration_index - _floors_below_base + _floor_base;
                snprintf(e->label, sizeof(e->label), "%d", e->floor);
                // use previous height as the last floor height
                e->height = height;
                _floor_calibration = 0;

                send_calibration_event(e);
                HR_LOGD("%s(%d): floor calibration finished ...\n", __FUNCTION__, __LINE__);

                motion_unregister_observer(&_calibration_observer);
            }
        }
    }
}

// {"BaseFloor":1,"FloorsBelow":2,"FloorsAbove":22}
static int _on_svc_message(void* payload, int len) {
    char* method = NULL;
    struct svc_action* act = NULL;
    cJSON *root = NULL, *params = NULL;
    printf("%s(%d): .......\n", __FUNCTION__, __LINE__);
    if (!payload || len == 0) {
        HR_LOGE("%s(%d): invalid method ...\n", __FUNCTION__, __LINE__);
        return -1;
    }

    HR_LOGD("%s(%d): payload:%s\n", __FUNCTION__, __LINE__, payload);
    root = cJSON_ParseWithLength((const char*)payload, len);
    if (!root) {
        return -1;
    }

    method = cJSON_GetStringValue(cJSON_GetObjectItem(root, "method"));
    if (!method) {
        cJSON_Delete(root);
        return -1;
    }

    params = cJSON_GetObjectItem(root, "params");
    if (!params) {
        cJSON_Delete(root);
        return -1;
    }

    for (act = &_svc_action_tbl[0]; act != NULL; act++) {
        // thing.service.StartAutoFloorCalibration
        HR_LOGE("name:%s vs method:%s\n", act->name, method);
        if (!strcmp(act->name, method)) {
            int rc = act->method(params);
            HR_LOGD("call method failed: %d\n", rc);
            break;
        }
    }

    cJSON_Delete(root);

    return 0;
}
// {"BaseFloor":1,"FloorsBelow":2,"FloorsAbove":22}
static int _StartAutoFloorCalibration(cJSON* params) {
    int floor_base = 1;
    int floors_below_base = 0;
    int floors_above_base = 1;
    double val = 0;
    if (!params) {
        return -1;
    }

    val = cJSON_GetNumberValue(cJSON_GetObjectItem(params, "BaseFloor"));
    if (isnan(val)) {
        return -1;
    }
    floor_base = (int)val;

    val = cJSON_GetNumberValue(cJSON_GetObjectItem(params, "FloorsBelow"));
    if (isnan(val)) {
        return -1;
    }
    floors_below_base = (int)val;

    val = cJSON_GetNumberValue(cJSON_GetObjectItem(params, "FloorsAbove"));
    if (isnan(val)) {
        return -1;
    }
    floors_above_base = (int)val;

    HR_LOGD("%s(%d): enter calibration: base: %d below: %d above: %d\n", __FUNCTION__, __LINE__, floor_base, floors_below_base, floors_above_base);

    motion_register_observer(&_calibration_observer);
    _floor_base = floor_base;
    _floors_below_base = floors_below_base;
    _floors_above_base = floors_above_base;
    _floor_calibration = 1;
    _floor_calibration_index = 0;
    // also  notify floor model
    floor_enter_calibration(floor_base, floors_below_base, floors_above_base);

    return 0;
}

// calibrate at special floor
// you should provide height relative to base floor
// should you can always use base floor
static int _CalibrateAtFloorManually(cJSON* params) {
    double val = 0;
    HR_LOGD("%s(%d): .......\n", __FUNCTION__, __LINE__);
    val = cJSON_GetNumberValue(cJSON_GetObjectItem(params, "Floor"));
    if (isnan(val)) {
    HR_LOGD("%s(%d): .......\n", __FUNCTION__, __LINE__);
        return -1;
    }

    motion_calibrate_at_floor((int)val);

    return 0;
}
static int _CalibrateAtHeightManually(cJSON* params) {
    double val = 0;
    HR_LOGD("%s(%d): .......\n", __FUNCTION__, __LINE__);
    val = cJSON_GetNumberValue(cJSON_GetObjectItem(params, "Height"));
    if (isnan(val)) {
    HR_LOGD("%s(%d): .......\n", __FUNCTION__, __LINE__);
        return -1;
    }

    motion_calibrate_at_height(val);

    return 0;
}

static struct iot_topic _iot_calibration_topics[_CALIBRATION_TOPIC_MAX] = {
    [CALIBRATION_TOPIC_AUTO_FLOOR_CALIBRATION_EVENT] = {
        .name = "event/AutoFloorCalibrationEvent/post",
        .topic = {0},
        .period = 0,
        .type = TOPIC_TYPE_PUBLISH,
        .callback.on_publish = _on_auto_floor_calibration_event_publish,
    },
    /*{
        .name = "event/autoFloorCalibrationEvent/post_reply",
        .topic = {0},
        .type = TOPIC_TYPE_SUBSCRIBE,
        .callback.on_message = _on_reply_message,
    },*/
    [CALIBRATION_TOPIC_START_AUTO_FLOOR_CALIBRATION] = {
        .name = "service/StartAutoFloorCalibration",
        .topic = {0},
        .type = TOPIC_TYPE_SUBSCRIBE,
        .callback.on_message = _on_svc_message,
    },
    [CALIBRATION_TOPIC_CALIBRATE_AT_FLOOR_MANUALLY] = {
        .name = "service/CalibrateAtFloorManually",
        .topic = {0},
        .type = TOPIC_TYPE_SUBSCRIBE,
        .callback.on_message = _on_svc_message,
    },
    [CALIBRATION_TOPIC_CALIBRATE_AT_HEIGHT_MANUALLY] = {
        .name = "service/CalibrateAtHeightManually",
        .topic = {0},
        .type = TOPIC_TYPE_SUBSCRIBE,
        .callback.on_message = _on_svc_message,
    },
};

int iot_topic_calibration_init(const char* public_key, const char* device_name) {
    if (!public_key || !device_name) {
        return -1;
    }

    for (size_t i = 0; i < ARRAY_SIZE(_iot_calibration_topics); i++) {
        struct iot_topic* t = &_iot_calibration_topics[i];
        snprintf(t->topic, sizeof(t->topic), "/sys/%s/%s/thing/%s", public_key, device_name, t->name);
        iot_topic_register(t);
    }

    return 0;
}
