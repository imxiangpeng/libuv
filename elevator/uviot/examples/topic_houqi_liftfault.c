
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "uviot.h"
/// publish every 10s
#include "cjson/cJSON.h"
#include "hr_log.h"

#define EVENT_FAULT_TOPIC_NAME "LiftFault"

static int _on_publish(void **payload, int *len) {
    printf("liftfault publish \n");

    cJSON *root = cJSON_CreateObject();
    if (!root) return -1;

 
    *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!*payload) return -1;

    *len = strlen(*payload);
    HR_LOGD("publish: %s\n", (char*)*payload);
    return 0;
}

static struct uviot_topic dm_topic_liftfault = {
    .name = EVENT_FAULT_TOPIC_NAME,
    .topic = "/API/V1/Up/" EVENT_FAULT_TOPIC_NAME,
    .period = 0,
    .type = TOPIC_TYPE_PUBLISH,
    .callback.on_publish = _on_publish,
};

int topic_houqi_liftfault_init(struct uviot* iot,const char* public_key, const char* device_name) {
  (void)public_key;
  (void)device_name;
    uviot_topic_register(iot, &dm_topic_liftfault);
    return 0;
}
