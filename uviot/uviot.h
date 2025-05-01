#ifndef UVIOT_H
#define UVIOT_H

struct iot_topic;

struct uv_loop_s;

struct uviot {
    char id[128]; // client id
    char server[128];
    int port;
    int alive_time;
    char username[128];
    char password[256];
};

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


// you should init uviot fields
struct uviot* uviot_alloc(struct uv_loop_s *loop);

// you should init iot fields and call uviot_prepare
// int uviot_prepare(struct iot* self);

// int uviot_init();

// prepare to run, you should init all fields in uviot
int uviot_prepare(struct uviot* self);

int uviot_release(struct uviot * iot);

int uviot_topic_register(struct uviot* self, const struct uviot_topic* topic);
int uviot_publish_async(struct uviot* self, const struct uviot_topic* topic);

const char* uviot_get_connection_ipv4_address(struct uviot *self);
const char* uviot_get_connection_mac_address(struct uviot *self);
#endif
