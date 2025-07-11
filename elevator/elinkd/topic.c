#include "topic.h"
#include "topic_property.h"
#include "topic_service.h"
#include "topic_event.h"

int topic_init(const char* public_key, const char* device_name) {
    (void)public_key;
    (void)device_name;

    topic_property_init(public_key, device_name);
    topic_service_init(public_key, device_name);
    topic_event_init(public_key, device_name);

    return 0;
}


static int _mid = 1;
int topic_generate_mid() {
    return _mid++;
}
