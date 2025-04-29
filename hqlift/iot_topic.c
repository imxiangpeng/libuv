

#include "iot_topic.h"

extern int iot_topic_property_init(const char* public_key, const char* device_name);
extern int iot_topic_calibration_init(const char* public_key, const char* device_name);

static int _mid = 1;
int iot_generate_mid() {
    return _mid++;
}

// pub
int iot_topic_init(const char* public_key, const char* device_name) {
    iot_topic_property_init(public_key, device_name);
    iot_topic_calibration_init(public_key, device_name);
    return 0;
}
