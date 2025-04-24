
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "dm_topic.h"
/// publish every 10s
#include "cjson/cJSON.h"
#include "hr_log.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static int _on_auto_floor_calibration_event_publish(void** payload, int* len) {
    struct tm tm;
    struct timespec ts;
    char tmp[256] = {0};
    cJSON *root = NULL, *param = NULL;
    root = cJSON_CreateObject();
    if (!root)
        return -1;

    snprintf(tmp, sizeof(tmp), "%d", iot_mid_generate());
    cJSON_AddStringToObject(root, "id", tmp);
    cJSON_AddStringToObject(root, "version", "1.0");

    param = cJSON_AddObjectToObject(root, "params");
    cJSON_AddNumberToObject(param, "Id", 0);
    cJSON_AddNumberToObject(param, "Floor", -1);
    cJSON_AddStringToObject(param, "Label", "B2");
    cJSON_AddNumberToObject(param, "Height", 3.9);

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
    printf("reply command message %d -> %s\n", len, (char*)payload);
    return 0;
}

static int _on_property_set_message(void* payload, int len) {
    printf("command message %d -> %s\n", len, (char*)payload);
    return 0;
}

static int _on_start_auto_floor_calibration_message(void* payload, int len) {
    printf("calibration command message %d -> %s\n", len, (char*)payload);
    return 0;
}

static struct dm_topic _iot_calibration_topics[] = {
    {
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
    {
        .name = "service/StartAutoFloorCalibration",
        .topic = {0},
        .type = TOPIC_TYPE_SUBSCRIBE,
        .callback.on_message = _on_start_auto_floor_calibration_message,
    },
};

int iot_topic_calibration_init(const char* public_key, const char* device_name) {

    if (!public_key || !device_name) {
        return -1;
    }

    for (size_t i = 0; i < ARRAY_SIZE(_iot_calibration_topics); i++) {
        struct dm_topic *t = &_iot_calibration_topics[i];
        snprintf(t->topic, sizeof(t->topic), "/sys/%s/%s/thing/%s", public_key, device_name, t->name);
        printf("topic:%s\n", t->topic);
        dm_topic_register(t);
    }

    return 0;
}
