
#include <stdio.h>
#include <string.h>
#include "dm_topic.h"
/// publish every 10s

static int _on_publish(void **payload, int *len) {
    printf("heartbeat publish \n");
    *payload = strdup("{\"name\":\"xiaohua\"}");
    *len = strlen(*payload);
    return 0;
}

struct dm_topic dm_topic_heartbeat = {
    .name = "Heartbeat",
    .topic = "/API/V1/Up/HeartBeat",
    .type = TOPIC_TYPE_PUBLISH,
    .callback.on_publish = _on_publish,
};