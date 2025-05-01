
#include <cjson/cJSON.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "iot_topic.h"

extern void topic_houqi_liftstate_post(void);

static int _on_command_message(void* payload, int len) {
    if (!payload || len == 0) {
        return -1;
    }

  
    return 0;
}

// static int _on_upload_record_list_publish(void** payload, int* len) {
//     return 0;
// }
struct iot_topic topic_command = {
    .name = "Command",
    .topic = {0},
    .type = TOPIC_TYPE_SUBSCRIBE,
    .callback.on_message = _on_command_message,
};

// struct iot_topic topic_upload_record_list = {
//     .name = "uploadRecordList",
//     .topic = {0},
//     .type = TOPIC_TYPE_PUBLISH,
//     .callback.on_publish = _on_upload_record_list_publish,
// };

int topic_houqi_command_init(struct iot* iot, const char* public_key, const char* device_name) {
    (void)public_key;
    (void)device_name;
    const char* serialno = "123";
    snprintf(topic_command.topic, sizeof(topic_command.topic), "/API/V1/Down/%s/Command", serialno);
    iot_topic_register(iot, &topic_command);
    return 0;
}
