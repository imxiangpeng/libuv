
#include <stdio.h>
#include <string.h>

#include "dm_topic.h"

static int _on_message(void* payload, int len) {
    printf("command message %d -> %s\n", len, (char*)payload);
    return 0;
}

struct dm_topic dm_topic_command = {
    .name = "Command",
    .topic = NULL,
    .type = TOPIC_TYPE_SUBSCRIBE,
    .callback.on_message = _on_message,
};

static char command_topic[256] = {0};
int dm_topic_command_init(void) {
    const char* serialno = "1234567890";
    snprintf(command_topic, sizeof(command_topic), "/API/V1/Down/%s/Command", serialno);
    dm_topic_command.topic = command_topic;
    dm_topic_register(&dm_topic_command);
    return 0;
}