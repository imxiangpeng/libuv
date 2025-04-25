
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "iot_topic.h"
/// publish every 10s
#include "cjson/cJSON.h"
#include "hr_log.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

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
    return 0;
}

static struct iot_topic _iot_property_topics[] = {
    {
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
    {
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

    return 0;
}
