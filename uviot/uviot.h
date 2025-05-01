#ifndef UVIOT_H
#define UVIOT_H

struct iot_topic;

struct uv_loop_s;

struct iot {
    char id[128]; // client id
    char server[128];
    int port;
    int alive_time;
    char username[128];
    char password[128];
};

// you should init uviot fields
struct iot* uviot_alloc(struct uv_loop_s *loop);

// you should init iot fields and call uviot_prepare
// int uviot_prepare(struct iot* self);

// int uviot_init();

// prepare to run, you should init all fields in uviot
int uviot_prepare(struct iot* self);

int uviot_release(struct iot * iot);

int uviot_topic_register(struct iot* self, const struct iot_topic* topic);
int uviot_publish_async(struct iot* self, const struct iot_topic* topic);
#endif
