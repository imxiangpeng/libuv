#include "dm_Device.h"

#include <string.h>

#include "dm_object.h"
#include "hr_log.h"

static int _X_CU_LockEnable = 0;
static int X_CU_LockEnable_getter(struct dm_object* self, struct dm_value* val) {
    HR_LOGD("%s(%d): this %p, val:%p\n", __FUNCTION__, __LINE__, self, val);
    dm_value_set_boolean(val, _X_CU_LockEnable);
    return 0;
}

static int X_CU_LockEnable_setter(struct dm_object* self, struct dm_value* val) {
    HR_LOGD("%s(%d): this %p, val:%d\n", __FUNCTION__, __LINE__, self, val->val.boolean);
    _X_CU_LockEnable = val->val.boolean;
    return 0;
}

int dm_Device_init(struct dm_object* parent) {
    struct dm_object* Device = dm_object_new("Device", DM_TYPE_OBJECT, dm_object_attribute, NULL, parent);
    dm_object_new("X_CU_LockEnable", DM_TYPE_BOOLEAN, X_CU_LockEnable_getter, X_CU_LockEnable_setter, Device);
    dm_Device_DeviceInfo_init(Device);
    dm_Device_X_CU_WAN_init(Device);

    dm_Device_WiFi(Device);
	
	dm_Device_MQTT_Client_init(Device);
    return 0;
}