
// mxp, 20250502, a smiple mqtt iot framework based on libuv library
// you should implement your topic only, do not care any other logic
// who to use:
// 1. call uviot_alloc to allocate struct uviot object;(you should pass your uv_loop handle)
// 2. fill information into struct uviot; (such as clientid, server url, port, alive time, username and password)
// 3. using uviot_topic_register to register your topics
// 4. call uviot_prepare connect your mqtt platform and poll to loop, then you can call uv_run
// 5. when you want to publish message, call uviot_publish_async, then you on_publish callback will be called;(uviot_publish_async is thread safe)
// 6. finally call uviot_release to release all resource
// 7. what's more, uviot_get_connection_ipv4/mac_address can be used to obtion mqtt connection information such as ip & mac

#ifndef UVIOT_H
#define UVIOT_H

struct iot_topic;

struct uv_loop_s;

struct uviot {
    char id[256]; // client id
    char server[128];
    int port;
    int alive_time;
    char username[128];
    char password[256];
    int tls_insecre;
    int tls_cert_reqs; // SSL_VERIFY_NONE/SSL_VERIFY_PEER
    const char* ca_file; // ca root certificate, such as /etc/ssl/certs/ca-certificates.crt
    const char* ca_path; // ca path, which contains all hash named ca certificate
    const char* certificate; // client certificate
    const char* certificate_key; // client certificate key
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
