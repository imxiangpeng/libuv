#ifndef DM_TOPIC_H
#define DM_TOPIC_H

struct dm_topic {
    const char *name;
    const char *topic;
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
// pub
int dm_topic_init(void);

// impl
int dm_topic_register(const struct dm_topic *topic);

#endif  // TOPIC_H