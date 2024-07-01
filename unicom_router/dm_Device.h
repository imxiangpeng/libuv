#ifndef DM_DEVICE_H
#define DM_DEVICE_H

#include "dm_object.h"

int dm_Device_init(struct dm_object *parent);
int dm_Device_DeviceInfo_init(struct dm_object *parent);
int dm_Device_X_CU_WAN_init(struct dm_object *parent);
#endif