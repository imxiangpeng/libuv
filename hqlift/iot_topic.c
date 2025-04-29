

#include "iot_topic.h"

extern int hq_topic_heartbeat_init(const char* public_key, const char* device_name);
extern int hq_topic_command_init(const char* public_key, const char* device_name);
extern int hq_topic_liftstate_init(const char* public_key, const char* device_name);
extern int hq_topic_liftfault_init(const char* public_key, const char* device_name);

static int _mid = 1;
int iot_generate_mid() {
    return _mid++;
}

// pub
int iot_topic_init(const char* public_key, const char* device_name) {
    hq_topic_heartbeat_init(public_key, device_name);
    hq_topic_command_init(public_key, device_name);
    hq_topic_liftstate_init(public_key, device_name);

    hq_topic_liftfault_init(public_key, device_name);
    return 0;
}
