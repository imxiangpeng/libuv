

#include "iot_topic.h"


extern int iot_topic_property_init(struct iot* iot, const char* public_key, const char* device_name);
extern int iot_topic_calibration_init(struct iot* iot, const char* public_key, const char* device_name);


int iot_mid_generate() {

    return 0;
}

// pub
int iot_topic_init(struct iot* iot, const char* public_key, const char* device_name) {
    iot_topic_property_init(iot, public_key, device_name);
    iot_topic_calibration_init(iot, public_key, device_name);
    return 0;
}
