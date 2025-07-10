#ifndef SYSTEM_H
#define SYSTEM_H
#include "property.h"


int system_property_build_timestamp(struct property*self, struct property_value *val);
int system_property_sw_version(struct property*self, struct property_value *val);

#endif