
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "cjson/cJSON.h"
#include "hr_log.h"
#include "iot_topic.h"
#include "uviot.h"

#define EVENT_RUNINFO_TOPIC_NAME "LiftRunInfo"

static int _on_publish(void** payload, int* len) {
    printf("liftruninfo publish \n");

    cJSON* root = NULL;

    if (!payload || !len)
        return -1;

    root = cJSON_CreateObject();
    if (!root)
        return -1;


    *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!*payload)
        return -1;

    *len = strlen(*payload);

    HR_LOGD("publish: %s\n", (char*)*payload);
    return 0;
}

static struct uviot_topic _topic_liftruninfo = {
    .name = EVENT_RUNINFO_TOPIC_NAME,
    .topic = "/API/V1/Up/" EVENT_RUNINFO_TOPIC_NAME,
    .period = 0,
    .type = TOPIC_TYPE_PUBLISH,
    .callback.on_publish = _on_publish,
};

struct uviot *_iot = NULL;
int topic_houqi_liftruninfo_init(struct uviot* iot,const char* public_key, const char* device_name) {
    (void)public_key;
    (void)device_name;
    _iot = iot;
    uviot_topic_register(iot, &_topic_liftruninfo);

    return 0;
}

// trigger publish immediately
void topic_houqi_liftruninfo_post(void) {
    uviot_publish_async(_iot, &_topic_liftruninfo);
}
