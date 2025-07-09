#include "topic.h"


// defined in topic_property.c
int topic_property_init(const char* public_key, const char* device_name);

int topic_init(const char* public_key, const char* device_name) {
    (void)public_key;
    (void)device_name;

    topic_property_init(public_key, device_name);

    return 0;
}


static int _mid = 1;
int topic_generate_mid() {
    return _mid++;
}
