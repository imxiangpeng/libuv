#include "dm_Device.h"

#include <string.h>
#include "dm_object.h"
#include "hr_log.h"

static int SerialNumber_getter(struct dm_object* self, struct dm_value* val) {
    HR_LOGD("%s(%d): this %p, val:%p\n", __FUNCTION__, __LINE__, self, val);
    dm_value_set_string_ext(val, "0C1102000001000", 1);
    return 0;
}

static int ModelName_getter(struct dm_object* self, struct dm_value* val) {
    HR_LOGD("%s(%d): this %p, val:%p\n", __FUNCTION__, __LINE__, self, val);
    dm_value_set_string_ext(val, "R630D", 1);
    return 0;
}


static int SoftwareVersion_getter(struct dm_object* self, struct dm_value* val) {
    HR_LOGD("%s(%d): this %p, val:%p\n", __FUNCTION__, __LINE__, self, val);
    dm_value_set_string_ext(val, "V1.0", 1);
    return 0;
}

static int HardwareVersion_getter(struct dm_object* self, struct dm_value* val) {
    HR_LOGD("%s(%d): this %p, val:%p\n", __FUNCTION__, __LINE__, self, val);
    dm_value_set_string_ext(val, "V1.0", 1);
    return 0;
}



static int X_CU_CUEI_getter(struct dm_object* self, struct dm_value* val) {
    HR_LOGD("%s(%d): this %p, val:%p\n", __FUNCTION__, __LINE__, self, val);
    dm_value_set_string_ext(val, "200037050001000", 1);
    return 0;
}

static int X_CU_DeviceType_getter(struct dm_object* self, struct dm_value* val) {
    HR_LOGD("%s(%d): this %p, val:%p\n", __FUNCTION__, __LINE__, self, val);
    dm_value_set_string_ext(val, "357", 1);
    return 0;
}

static int X_CU_ManufacturerCode_getter(struct dm_object* self, struct dm_value* val) {
    HR_LOGD("%s(%d): this %p, val:%p\n", __FUNCTION__, __LINE__, self, val);
    dm_value_set_string_ext(val, "077", 1);
    return 0;
}




static int X_CU_ProductId_getter(struct dm_object* self, struct dm_value* val) {
    HR_LOGD("%s(%d): this %p, val:%p\n", __FUNCTION__, __LINE__, self, val);
    dm_value_set_string_ext(val, "077", 1);
    return 0;
}

static int X_CU_ProtocolType_getter(struct dm_object* self, struct dm_value* val) {
    HR_LOGD("%s(%d): this %p, val:%p\n", __FUNCTION__, __LINE__, self, val);
    dm_value_set_string_ext(val, "1", 1);
    return 0;
}


int dm_Device_DeviceInfo_init(struct dm_object *parent) {

    struct dm_object* DeviceInfo = dm_object_new("DeviceInfo", DM_TYPE_OBJECT, dm_object_attribute, NULL, parent);
    dm_object_new("SerialNumber", DM_TYPE_STRING, SerialNumber_getter, NULL, DeviceInfo);
    dm_object_new("ModelName", DM_TYPE_STRING, ModelName_getter, NULL, DeviceInfo);
    dm_object_new("SoftwareVersion", DM_TYPE_STRING, SoftwareVersion_getter, NULL, DeviceInfo);
    dm_object_new("HardwareVersion", DM_TYPE_STRING, HardwareVersion_getter, NULL, DeviceInfo);
    dm_object_new("X_CU_FriendlyName", DM_TYPE_STRING, SerialNumber_getter, NULL, DeviceInfo);
    dm_object_new("X_CU_CUEI", DM_TYPE_STRING, X_CU_CUEI_getter, NULL, DeviceInfo);
    dm_object_new("X_CU_DeviceType", DM_TYPE_STRING, X_CU_DeviceType_getter, NULL, DeviceInfo);
    dm_object_new("X_CU_ManufacturerCode", DM_TYPE_STRING, X_CU_ManufacturerCode_getter, NULL, DeviceInfo);
    dm_object_new("X_CU_ProductId", DM_TYPE_STRING, X_CU_ProductId_getter, NULL, DeviceInfo);
    dm_object_new("X_CU_ProtocolType", DM_TYPE_STRING, X_CU_ProtocolType_getter, NULL, DeviceInfo);

    return 0;
}