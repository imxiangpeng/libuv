#include "dm_Device.h"

#include "hr_log.h"

static int ConnectionType_getter(struct dm_object* self, struct dm_value* val) {
    HR_LOGD("%s(%d): this %p, val:%p\n", __FUNCTION__, __LINE__, self, val);
    dm_value_set_string_ext(val, "DHCP", 1);
    return 0;
}

static int MAC_getter(struct dm_object* self, struct dm_value* val) {
    HR_LOGD("%s(%d): this %p, val:%p\n", __FUNCTION__, __LINE__, self, val);
    dm_value_set_string_ext(val, "341CF0434923", 1);
    return 0;
}



int dm_Device_X_CU_WAN_init(struct dm_object *parent) {
    struct dm_object* X_CU_WAN = dm_object_new("X_CU_WAN", DM_TYPE_OBJECT, dm_object_attribute, NULL, parent);
    dm_object_new("ConnectionType", DM_TYPE_STRING, ConnectionType_getter, NULL, X_CU_WAN);
    dm_object_new("MAC", DM_TYPE_STRING, MAC_getter, NULL, X_CU_WAN);

    return 0;
}