#ifndef IOT_TOPIC_H
#define IOT_TOPIC_H
#include "uviot.h"

// pub
int iot_topic_init(struct uviot*iot, const char* public_key, const char* device_name);

#endif