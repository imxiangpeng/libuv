#ifndef SYSTEM_H
#define SYSTEM_H
#include "property.h"


int system_property_serial(struct property*self);
int system_property_build_timestamp(struct property*self);
int system_property_sw_version(struct property*self);

int system_service_reboot(void);
int system_service_start_ssh_tunnel(void);
int system_service_stop_ssh_tunnel(void);
#endif