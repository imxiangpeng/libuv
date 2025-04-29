
#include <stdio.h>
#include <string.h>

#include "iot_topic.h"

static int _on_message(void* payload, int len) {
    printf("command message %d -> %s\n", len, (char*)payload);
    return 0;
}

struct iot_topic topic_command = {
    .name = "Command",
    .topic = {0},
    .type = TOPIC_TYPE_SUBSCRIBE,
    .callback.on_message = _on_message,
};

int hq_topic_command_init(const char* public_key, const char* device_name) {
  (void)public_key;
  (void)device_name;
    const char* serialno = "1234567890";
    snprintf(topic_command.topic, sizeof(topic_command.topic), "/API/V1/Down/%s/Command", serialno);
    iot_topic_register(&topic_command);
    return 0;
}
