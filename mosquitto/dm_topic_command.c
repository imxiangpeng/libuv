
#include <stdio.h>
#include <string.h>
#include "dm_topic.h"
/// publish every 10s

static int _on_message(void *payload, int len) {
    printf("command message %d -> %s\n", len , (char*)payload);
    return 0;
}

struct dm_topic dm_topic_command = {
    .name = "Command",
    .topic = "/API/V1/Down/1234567890/Command",
    .type = TOPIC_TYPE_SUBSCRIBE,
    .callback.on_message = _on_message,
};
