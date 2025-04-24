
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "dm_topic.h"
/// publish every 10s
#include "cjson/cJSON.h"
#include "hr_log.h"

static int _on_publish(void** payload, int* len) {
    struct tm tm;
    struct timespec ts;
    printf("liftstate publish \n");
    char tmp[256] = {0};
    cJSON *root = NULL, *param = NULL;
    root = cJSON_CreateObject();
    if (!root) return -1;

    snprintf(tmp, sizeof(tmp), "%d", iot_mid_generate());
    cJSON_AddStringToObject(root, "id", tmp);
    cJSON_AddStringToObject(root, "version", "1.0");
    // cJSON_AddStringToObject(root, "method", "thing.event.property.post");
    
    param = cJSON_AddObjectToObject(root, "params");
    cJSON_AddNumberToObject(param, "pressure", 97.973);
    cJSON_AddNumberToObject(param, "temperature", 28.33);
    //cJSON_AddNumberToObject(param, "door", 0);

    *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!*payload) return -1;

    *len = strlen(*payload);
    HR_LOGD("publish: %s\n", *payload);
#if 0    
    *payload = strdup("{\"name\":\"xiaohua\"}");
    *len = strlen(*payload);
#endif
    return 0;
}

static int _on_post_replay_message(void* payload, int len) {
    printf("post reply command message %d -> %s\n", len, (char*)payload);
    return 0;
}

static int _on_message(void* payload, int len) {
    printf("command message %d -> %s\n", len, (char*)payload);
    return 0;
}

struct dm_topic iot_service_property_set = {
    .name = "service/property/set",
    .topic = NULL,
    .type = TOPIC_TYPE_SUBSCRIBE,
    .callback.on_message = _on_message,
};

static struct dm_topic iot_event_property_post = {
    .name = "event/property/post",
    .topic = NULL,
    // loop every second, only report when data have been changed
    // or we should disable period and use trigger only
    .period = 1000 * 10,
    .type = TOPIC_TYPE_PUBLISH,
    .callback.on_publish = _on_publish,
};

struct dm_topic iot_event_property_post_reply = {
    .name = "event/property/post_reply",
    .topic = NULL,
    .type = TOPIC_TYPE_SUBSCRIBE,
    .callback.on_message = _on_message,
};



static char event_property_post[256] = {0};
static char event_property_post_reply[256] = {0};
static char service_property_set[256] = {0};

int iot_topic_property_init(const char* public_key, const char* device_name) {
    
    iot_event_property_post.name = "event/property/post";
    iot_event_property_post_reply.name = "event/property/post_reply";
    iot_service_property_set.name = "service/property/set";
    snprintf(event_property_post, sizeof(event_property_post), "/sys/%s/%s/thing/event/property/post", public_key, device_name);
    snprintf(event_property_post_reply, sizeof(event_property_post_reply), "/sys/%s/%s/thing/event/property/post_reply", public_key, device_name);
    snprintf(service_property_set, sizeof(service_property_set), "/sys/%s/%s/thing/service/property/set", public_key, device_name);
    
    iot_event_property_post.topic = event_property_post;
    iot_event_property_post_reply.topic = event_property_post_reply;
    iot_service_property_set.topic = service_property_set;

    dm_topic_register(&iot_event_property_post);
    dm_topic_register(&iot_event_property_post_reply);
    dm_topic_register(&iot_service_property_set);

    return 0;
}
