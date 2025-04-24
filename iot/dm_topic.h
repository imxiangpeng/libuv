#ifndef DM_TOPIC_H
#define DM_TOPIC_H

struct dm_topic {
    char name[128];
    char topic[256];
    int period;
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
int dm_topic_init(const char* public_key, const char* device_name);

// impl
int dm_topic_register(const struct dm_topic *topic);

#endif  // TOPIC_H