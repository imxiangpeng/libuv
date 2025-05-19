
#include <stdio.h>
#include <string.h>
#include <time.h>

/// publish every 10s
#include "cjson/cJSON.h"
#include "hr_log.h"
#include "uviot.h"

#define EVENT_LIFTSTATE_TOPIC_NAME "LiftState"

// report when begin and finish
// stationary -> up/down
// up/down -> stationary
static int _on_publish(void** payload, int* len) {
    printf("liftstate publish \n");
    // char tmp[256] = {0};

    cJSON* root = cJSON_CreateObject();
    if (!root)
        return -1;

    cJSON_AddStringToObject(root, "type", EVENT_LIFTSTATE_TOPIC_NAME);
    *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!*payload) {
        return -1;
    }
    *len = strlen(*payload);
    HR_LOGD("publish: %s\n", (char*)*payload);
#if 0
    *payload = strdup("{\"name\":\"xiaohua\"}");
    *len = strlen(*payload);
#endif
    return 0;
}

static struct uviot_topic _topic_liftstate = {
    .name = EVENT_LIFTSTATE_TOPIC_NAME,
    .topic = "/API/V1/Up/" EVENT_LIFTSTATE_TOPIC_NAME,
    .period = 1000,  // 大华好像配置的是 500ms
    .type = TOPIC_TYPE_PUBLISH,
    .callback.on_publish = _on_publish,
};

static struct uviot* _iot = NULL;
int topic_houqi_liftstate_init(struct uviot* iot, const char* public_key, const char* device_name) {
    (void)public_key;
    (void)device_name;
    _iot = iot;
    uviot_topic_register(iot, &_topic_liftstate);

    return 0;
}

// trigger publish immediately
void topic_houqi_liftstate_post(void) {
    uviot_publish_async(_iot, &_topic_liftstate);
}
