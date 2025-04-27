
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "iot_topic.h"
/// publish every 10s
#include "cjson/cJSON.h"
#include "hr_log.h"

#include "motion.h"
#include "sensor.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

enum {
    PROPERTY_TOPIC_POST = 0,
    PROPERTY_TOPIC_SET,
    _PROPERTY_TOPIC_MAX,
};

static int _property_imu_calibration = 0;
static double _property_G = 9.81;

static void _iot_motion_observer_on_sensor_calibration(struct motion_sensor_calibration_event* data);

static struct motion_observer _iot_property_observer = {
    .on_sensor_calibration = _iot_motion_observer_on_sensor_calibration,
};

static int _on_publish(void** payload, int* len) {
    char tmp[256] = {0};
    cJSON *root = NULL, *param = NULL;

    root = cJSON_CreateObject();
    if (!root)
        return -1;

    // test code, please only response when needed
    snprintf(tmp, sizeof(tmp), "%d", iot_generate_mid());
    cJSON_AddStringToObject(root, "id", tmp);
    cJSON_AddStringToObject(root, "version", "1.0");

    param = cJSON_AddObjectToObject(root, "params");
    cJSON_AddNumberToObject(param, "pressure", 97.973);
    cJSON_AddNumberToObject(param, "temperature", 28.33);
    cJSON_AddNumberToObject(param, "imu_calibration", _property_imu_calibration);
    cJSON_AddNumberToObject(param, "G", _property_G);

    *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!*payload)
        return -1;

    *len = strlen(*payload);
    HR_LOGD("publish: %s\n", *payload);
#if 0
    *payload = strdup("{\"name\":\"xiaohua\"}");
    *len = strlen(*payload);
#endif
    return 0;
}

static int _on_reply_message(void* payload, int len) {
    printf("reply message %d -> %s\n", len, (char*)payload);
    return 0;
}

static int _on_property_set_message(void* payload, int len) {
    printf("set message %d -> %s\n", len, (char*)payload);
    char* method = NULL;
    double val = 0;
    cJSON *root = NULL, *params = NULL, *ele = NULL;
    if (!payload || len == 0) {
        HR_LOGE("%s(%d): invalid method ...\n", __FUNCTION__, __LINE__);
        return -1;
    }

    root = cJSON_ParseWithLength((const char*)payload, len);
    if (!root) {
        return -1;
    }

    method = cJSON_GetStringValue(cJSON_GetObjectItem(root, "method"));
    if (!method || 0 != strcmp("thing.service.property.set", method)) {
        cJSON_Delete(root);
        return -1;
    }

    params = cJSON_GetObjectItem(root, "params");
    if (!params) {
        cJSON_Delete(root);
        return -1;
    }

    cJSON_ArrayForEach(ele, params) {
        HR_LOGD("ele: %s -> type:%d\n", ele->string, ele->type);

    }
    val = cJSON_GetNumberValue(cJSON_GetObjectItem(params, "imu_calibration"));
    if (isnan(val)) {
        cJSON_Delete(root);
        return -1;
    }

    cJSON_Delete(root);
    HR_LOGD("%s(%d): enter calibration val: %d\n", __FUNCTION__, __LINE__, val);

    if (val == 1) {
        motion_enter_sensor_calibration();
    }

    return 0;
}

static struct iot_topic _iot_property_topics[_PROPERTY_TOPIC_MAX] = {
    [PROPERTY_TOPIC_POST] = {
        .name = "event/property/post",
        .topic = {0},
        .period = 0,
        .auto_public = 1,
        .type = TOPIC_TYPE_PUBLISH,
        .callback.on_publish = _on_publish,
    },
    /*{
        .name = "event/property/post_reply",
        .topic = {0},
        .type = TOPIC_TYPE_SUBSCRIBE,
        .callback.on_message = _on_reply_message,
    },*/
    [PROPERTY_TOPIC_SET] = {
        .name = "service/property/set",
        .topic = {0},
        .type = TOPIC_TYPE_SUBSCRIBE,
        .callback.on_message = _on_property_set_message,
    }};

int iot_topic_property_init(const char* public_key, const char* device_name) {
    if (!public_key || !device_name) {
        return -1;
    }

    for (size_t i = 0; i < ARRAY_SIZE(_iot_property_topics); i++) {
        struct iot_topic* t = &_iot_property_topics[i];
        snprintf(t->topic, sizeof(t->topic), "/sys/%s/%s/thing/%s", public_key, device_name, t->name);
        iot_topic_register(t);
    }

    motion_register_observer(&_iot_property_observer);

    return 0;
}

static void _iot_motion_observer_on_sensor_calibration(struct motion_sensor_calibration_event* data) {
    if (!data)
        return;
    HR_LOGD("%s(%d): sensor:%d, is calibration:%d\n", __FUNCTION__, __LINE__, data->type, data->is_calibration);

    if (data->type == SENSOR_ACCELEROMETER) {
        _property_imu_calibration = data->is_calibration;
        _property_G = round(data->value[0] * 10000) / 10000;
        iot_topic_public_async(&_iot_property_topics[PROPERTY_TOPIC_POST]);
    }
}