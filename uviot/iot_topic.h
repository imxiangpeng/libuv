#ifndef IOT_TOPIC_H
#define IOT_TOPIC_H
#include "uviot.h"
#if 0
struct uviot;

struct uviot_topic {
    char name[128];
    char topic[256];
    int period;
    int auto_public; // auto publish when connected
    enum topic_type {
        TOPIC_TYPE_PUBLISH = 0,
        TOPIC_TYPE_SUBSCRIBE
    } type;
    // on start
    // on stop
    union {
        // called with message on subscribed topic
        int (*on_message)(void *payload, int len);
        // called before publish topic
        int (*on_publish)(void **payload, int *len);
    } callback;
};

int iot_generate_mid();

#endif
// pub
int iot_topic_init(struct uviot*iot, const char* public_key, const char* device_name);

// impl
int iot_topic_register(struct uviot*iot, const struct uviot_topic *topic);

int iot_topic_public_async(struct uviot*iot, const struct uviot_topic* topic);
#endif
