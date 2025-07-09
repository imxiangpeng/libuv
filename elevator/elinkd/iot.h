#ifndef IOT_H
#define IOT_H

struct topic {
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

int iot_init();

int iot_deinit();

int iot_topic_register(const struct topic* topic);

int iot_topic_publish_async(const struct topic* topic);

#endif