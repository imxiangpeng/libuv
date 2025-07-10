#ifndef TOPIC_H
#define TOPIC_H
#include "iot.h"


int topic_generate_mid();

int topic_init(const char* public_key, const char* device_name);


#endif