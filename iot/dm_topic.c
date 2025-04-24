#include "dm_topic.h"

extern int dm_topic_heartbeat_init(void);
extern int dm_topic_command_init(void);
extern int dm_topic_liftfault_init(void);

extern int iot_topic_property_init(const char* public_key, const char* device_name);

static int _mid = 1;
int iot_mid_generate() {
    return _mid++;
}

int dm_topic_init(const char* public_key, const char* device_name) {

    iot_topic_property_init(public_key, device_name);

    return 0;
}