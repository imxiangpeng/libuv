#ifndef TOPIC_EVENT_H
#define TOPIC_EVENT_H
int topic_event_init(const char* public_key, const char* device_name);

void topic_event_publish_fault_event(const char* message);
#endif