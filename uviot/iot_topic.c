#include <stdio.h>
#include "uviot.h"
#include "iot_topic.h"

extern int topic_houqi_heartbeat_init(struct iot* iot, const char* public_key, const char* device_name);
extern int topic_houqi_command_init(struct iot* iot, const char* public_key, const char* device_name);
extern int topic_houqi_liftstate_init(struct iot* iot, const char* public_key, const char* device_name);
extern int topic_houqi_liftfault_init(struct iot* iot, const char* public_key, const char* device_name);

int topic_houqi_liftruninfo_init(struct iot* iot, const char* public_key, const char* device_name);

static int _mid = 1;
int iot_generate_mid() {
    return _mid++;
}

// pub
int iot_topic_init(struct iot* iot, const char* public_key, const char* device_name) {
    (void)iot;
    topic_houqi_heartbeat_init(iot, public_key, device_name);
    topic_houqi_command_init(iot, public_key, device_name);
    topic_houqi_liftstate_init(iot, public_key, device_name);

    topic_houqi_liftfault_init(iot, public_key, device_name);
    topic_houqi_liftruninfo_init(iot, public_key, device_name);
    return 0;
}

// impl
int iot_topic_register(struct iot* iot, const struct iot_topic* topic) {
    printf("%s(%d): ...............\n", __FUNCTION__, __LINE__);
    return uviot_topic_register(iot, topic);
}

int iot_topic_public_async(struct iot* iot, const struct iot_topic* topic) {
    return uviot_publish_async(iot, topic);
}
