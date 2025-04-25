#ifndef IOT_MOSQUITTO_H
#define IOT_MOSQUITTO_H

#include <stdbool.h>

#include <uv.h>

#include "hr_list.h"
#include "iot.h"
#include "iot_topic.h"

struct iot_mosquitto {
    struct iot self;
    bool auto_reconnect;
    int sock;  // mosquitto socket
    struct mosquitto* mosq;
    
    uv_loop_t *loop;
    uv_timer_t timer;
    uv_poll_t poll;
    int pevents;
    
    struct hr_list_head topic_head;
};

struct iot* iot_mosquitto_new();

int iot_mosquitto_prepare(struct iot* self);
int iot_mosquitto_run(struct iot* self, uv_loop_t * loop);
int iot_mosquitto_topic_register(struct iot* self, const struct iot_topic *topic);
#endif