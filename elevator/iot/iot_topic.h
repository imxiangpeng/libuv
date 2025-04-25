#ifndef IOT_TOPIC_H
#define IOT_TOPIC_H

struct iot;

struct iot_topic {
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


int iot_mid_generate();

// pub
int iot_topic_init(struct iot* iot, const char* public_key, const char* device_name);

// impl
int iot_topic_register(struct iot* iot, const struct iot_topic *topic);

#endif