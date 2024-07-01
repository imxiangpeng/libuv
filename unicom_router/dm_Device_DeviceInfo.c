#include "dm_Device.h"

#include <string.h>
#include "dm_object.h"
#include "hr_log.h"

static int SerialNumber_getter(struct dm_object* self, struct dm_value* val) {
    HR_LOGD("%s(%d): this %p, val:%p\n", __FUNCTION__, __LINE__, self, val);
    dm_value_set_string_ext(val, "0C1102000001001", 1);
    // dm_value_set_string(val, "1234567890");
    return 0;
}

static int X_CU_CUEI_getter(struct dm_object* self, struct dm_value* val) {
    HR_LOGD("%s(%d): this %p, val:%p\n", __FUNCTION__, __LINE__, self, val);
    dm_value_set_string_ext(val, "200037050001000", 1);
    // dm_value_set_string(val, "1234567890");
    return 0;
}



int dm_Device_DeviceInfo_init(struct dm_object *parent) {

    struct dm_object* DeviceInfo = dm_object_new("DeviceInfo", DM_TYPE_OBJECT, dm_object_attribute, NULL, parent);
    struct dm_object* SerialNumber = dm_object_new("SerialNumber", DM_TYPE_STRING, SerialNumber_getter, NULL, DeviceInfo);
    struct dm_object* ModelName = dm_object_new("ModelName", DM_TYPE_STRING, SerialNumber_getter, NULL, DeviceInfo);
    struct dm_object* SoftwareVersion = dm_object_new("SoftwareVersion", DM_TYPE_STRING, SerialNumber_getter, NULL, DeviceInfo);
    dm_object_new("X_CU_FriendlyName", DM_TYPE_STRING, SerialNumber_getter, NULL, DeviceInfo);
    dm_object_new("X_CU_CUEI", DM_TYPE_STRING, X_CU_CUEI_getter, NULL, DeviceInfo);

    return 0;
}