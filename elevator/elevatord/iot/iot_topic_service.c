// mxp, 20250415, floor model

#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "file_util.h"
#include "iot_topic.h"
/// publish every 10s
#include "cjson/cJSON.h"
#include "floor.h"
#include "hr_list.h"
#include "hr_log.h"
#include "motion.h"
#include "uviot.h"

// defined in iot_topic_property.c
extern void report_floor_model_property();
extern void report_hqliftd_config_property();

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define SVC_METHOD_START_AUTO_FLOOR_CALIBRATION "thing.service.StartAutoFloorCalibration"
#define SVC_METHOD_CALIBRATE_AT_FLOOR_MANUALLY "thing.service.CalibrateAtFloorManually"
#define SVC_METHOD_CALIBRATE_AT_HEIGHT_MANUALLY "thing.service.CalibrateAtHeightManually"
#define SVC_METHOD_GET_HQLIFTD_CONFIG "thing.service.GetHQLiftdConfig"
#define SVC_METHOD_SET_FLOOR_MODEL "thing.service.SetFloorModel"

enum {
    CALIBRATION_TOPIC_AUTO_FLOOR_CALIBRATION_EVENT = 0,
    CALIBRATION_TOPIC_START_AUTO_FLOOR_CALIBRATION,
    CALIBRATION_TOPIC_CALIBRATE_AT_FLOOR_MANUALLY,
    CALIBRATION_TOPIC_CALIBRATE_AT_HEIGHT_MANUALLY,
    GET_HQLIFTD_CONFIG,
    // GET_HQLIFTD_CONFIG_REPLY,
    SET_FLOOR_MODEL,
    _SERVICE_TOPIC_MAX
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
    double pressure;
    struct hr_list_head entry;
};

static struct uviot_topic _iot_service_topics[_SERVICE_TOPIC_MAX];

static HR_LIST_HEAD(_auto_floor_calibration_message_queue);
static pthread_mutex_t _queue_mutex;
static struct uviot* _iot = NULL;
static struct uviot_topic _iot_service_topics[];

static int _StartAutoFloorCalibration(cJSON* params);
static int _CalibrateAtFloorManually(cJSON* params);
static int _CalibrateAtHeightManually(cJSON* params);
static int _GetHQLiftdConfig(cJSON* params);
static int _SetFloorModel(cJSON* params);
static struct svc_action _svc_action_tbl[] = {
    {SVC_METHOD_START_AUTO_FLOOR_CALIBRATION, _StartAutoFloorCalibration},
    {SVC_METHOD_CALIBRATE_AT_FLOOR_MANUALLY, _CalibrateAtFloorManually},
    {SVC_METHOD_CALIBRATE_AT_HEIGHT_MANUALLY, _CalibrateAtHeightManually},
    {SVC_METHOD_GET_HQLIFTD_CONFIG, _GetHQLiftdConfig},
    {SVC_METHOD_SET_FLOOR_MODEL, _SetFloorModel},
    {NULL, NULL},  // keep it
};

static char _stored_id[64] = {0};
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

    pthread_mutex_lock(&_queue_mutex);
    hr_list_del(&e->entry);
    pthread_mutex_unlock(&_queue_mutex);

    HR_INIT_LIST_HEAD(&e->entry);

    free(e);
}

static int send_calibration_event(struct calibration_event* m) {
    pthread_mutex_lock(&_queue_mutex);
    hr_list_add_tail(&m->entry, &_auto_floor_calibration_message_queue);
    pthread_mutex_unlock(&_queue_mutex);
    return uviot_publish_async(_iot, &_iot_service_topics[CALIBRATION_TOPIC_AUTO_FLOOR_CALIBRATION_EVENT]);
}

static int _on_auto_floor_calibration_event_publish(void** payload, int* len) {
    char tmp[256] = {0};
    struct calibration_event* e = NULL;
    cJSON *root = NULL, *param = NULL;

    // not new message, no need publish
    if (hr_list_empty(&_auto_floor_calibration_message_queue)) {
        return -1;
    }

    hr_list_for_each_entry(e, &_auto_floor_calibration_message_queue, entry) {
        HR_LOGE("%s(%d): mxp id:%d, floor:%d, label:%s, height:%f, pressure:%f\n", __FUNCTION__, __LINE__, e->id, e->floor, e->label, e->height, e->pressure);
    }

    e = hr_list_first_entry(&_auto_floor_calibration_message_queue, struct calibration_event, entry);

    root = cJSON_CreateObject();
    if (!root) {
        calibration_event_free(e);
        return -1;
    }

    snprintf(tmp, sizeof(tmp), "%d", iot_generate_mid());
    cJSON_AddStringToObject(root, "id", tmp);
    cJSON_AddStringToObject(root, "version", "1.0.0");

    param = cJSON_AddObjectToObject(root, "params");
    cJSON_AddNumberToObject(param, "Id", e->id);
    cJSON_AddNumberToObject(param, "Floor", e->floor);
    cJSON_AddStringToObject(param, "Label", e->label);
    cJSON_AddNumberToObject(param, "Height", e->height);
    cJSON_AddNumberToObject(param, "Pressure", e->pressure);

    // free the message
    calibration_event_free(e);

    *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!*payload)
        return -1;

    *len = strlen(*payload);
    HR_LOGD("publish: %s\n", (char*)*payload);
#if 1
    if (!hr_list_empty(&_auto_floor_calibration_message_queue)) {
        // when queue is not empty, we should trigger again
        // because uv_async merges multiple requests and triggers the callback only once
        return uviot_publish_async(_iot, &_iot_service_topics[CALIBRATION_TOPIC_AUTO_FLOOR_CALIBRATION_EVENT]);
    }
#endif
    return 0;
}

static int _on_reply_message(void* payload, int len) {
    if (!payload)
        return -1;

    printf("reply message %d -> %s\n", len, (const char*)payload);
    return 0;
}
// {"BaseFloor":1,"FloorsBelow":2,"FloorsAbove":22}
static int _on_svc_message(void* payload, int len) {
    char* id = NULL;
    char* method = NULL;
    struct svc_action* act = NULL;
    cJSON *root = NULL, *params = NULL;

    if (!payload || len == 0) {
        HR_LOGE("%s(%d): invalid method ...\n", __FUNCTION__, __LINE__);
        return -1;
    }

    HR_LOGD("%s(%d): payload:%s\n", __FUNCTION__, __LINE__, (const char*)payload);
    root = cJSON_ParseWithLength((const char*)payload, len);
    if (!root) {
        return -1;
    }

    id = cJSON_GetStringValue(cJSON_GetObjectItem(root, "id"));
    if (!id) {
        cJSON_Delete(root);
        return -1;
    }

    snprintf(_stored_id, sizeof(_stored_id), "%s", id);

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
        if (!strcmp(act->name, method)) {
            /*int rc =*/act->method(params);
            break;
        }
    }

    cJSON_Delete(root);

    return 0;
}

static void _on_floor_calibration_event(int id, int floor, const char* label, double height, double pressure, int completed) {
    struct calibration_event* e = NULL;
    if (!label) {
        return;
    }
    HR_LOGD("%s(%d): auto calibration event:%d %d %s %f\n", __FUNCTION__, __LINE__, id, floor, label, height);

    e = calibration_event_alloc();
    if (!e) {
        return;
    }
    e->id = id;
    e->floor = floor;
    e->height = height;
    e->pressure = pressure;

    if (label) {
        snprintf(e->label, sizeof(e->label), "%s", label);
    } else {
        snprintf(e->label, sizeof(e->label), "%d", floor);
    }
    send_calibration_event(e);

    if (completed != 0) {
        // report floor model data

        report_floor_model_property();
    }
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

    // also  notify floor model
    floor_enter_calibration_with_callback(floor_base, floors_below_base, floors_above_base, _on_floor_calibration_event);

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

static int _GetHQLiftdConfig(cJSON* params) {
    (void)params;
    report_hqliftd_config_property();
    // uviot_publish_async(_iot, &_iot_service_topics[GET_HQLIFTD_CONFIG_REPLY]);
    return 0;
}

static int _SetFloorModel(cJSON* params) {
    (void)params;
    #warning "xxxxxxxxxxxxxxxxxxxxxxxx"
    // floor_update_floor_model_data();
    return 0;
}
#if 0
// https://help.aliyun.com/zh/iot/user-guide/device-properties-events-and-services#section-jkt-v1x-y2b
static int _on_get_hqliftd_config_publish(void** payload, int* len) {
    cJSON *root = NULL, *data = NULL;
    (void)data;
    (void)payload;
    (void)len;

    HR_LOGD("%s(%d): .......\n", __FUNCTION__, __LINE__);
    root = cJSON_CreateObject();
    if (!root)
        return -1;

    // test code, please only response when needed
    cJSON_AddStringToObject(root, "id", _stored_id);
    cJSON_AddStringToObject(root, "version", "1.0.0");
    cJSON_AddStringToObject(root, "message", "success");

    data = cJSON_AddObjectToObject(root, "data");
    
    cJSON_AddStringToObject(data, "data", "xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\nsuccess");
    
    *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!*payload)
        return -1;

    *len = strlen(*payload);

    return 0;
}
#endif
static struct uviot_topic _iot_service_topics[_SERVICE_TOPIC_MAX] = {
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
    [GET_HQLIFTD_CONFIG] = {
        .name = "service/GetHQLiftdConfig",
        .topic = {0},
        .type = TOPIC_TYPE_SUBSCRIBE,
        .callback.on_message = _on_svc_message,
    },
#if 0    
    [GET_HQLIFTD_CONFIG_REPLY] = {
        .name = "service/GetHQLiftdConfig_reply",
        .topic = {0},
        .type = TOPIC_TYPE_PUBLISH,
        .callback.on_publish = _on_get_hqliftd_config_publish,
    },
#endif
    [SET_HQLIFTD_CONFIG] = {
        .name = "service/SetHQLiftdConfig",
        .topic = {0},
        .type = TOPIC_TYPE_SUBSCRIBE,
        .callback.on_message = _on_svc_message,
    }};

int iot_topic_service_init(struct uviot* iot, const char* public_key, const char* device_name) {
    (void)iot;
    if (!public_key || !device_name) {
        return -1;
    }
    _iot = iot;
    pthread_mutex_init(&_queue_mutex, NULL);
    for (size_t i = 0; i < ARRAY_SIZE(_iot_service_topics); i++) {
        struct uviot_topic* t = &_iot_service_topics[i];
        snprintf(t->topic, sizeof(t->topic), "/sys/%s/%s/thing/%s", public_key, device_name, t->name);
        uviot_topic_register(iot, t);
    }

    return 0;
}
