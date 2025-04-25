#ifndef IOT_MOSQUITTO_H
#define IOT_MOSQUITTO_H

#include <stdbool.h>

#include <uv.h>

#include "hr_list.h"
#include "iot.h"
#include "iot_topic.h"

struct iot* iot_mosquitto_new();

int iot_mosquitto_prepare(struct iot* self);
int iot_mosquitto_run(struct iot* self, uv_loop_t * loop);
int iot_mosquitto_topic_register(struct iot* self, const struct iot_topic *topic);
int iot_mosquitto_public_async(struct iot* self, const struct iot_topic* topic);
#endif