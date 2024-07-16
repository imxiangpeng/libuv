#include <stdio.h>
#include <string.h>

#include "dm_Device.h"
#include "dm_object.h"
#include "hr_list.h"
#include "hr_log.h"

extern void uc_dm_notify(const char* params[], size_t size);

static int OperatingFrequencyBand_getter(struct dm_object* self, struct dm_value* val) {
    HR_LOGD("%s(%d): this %p -> parent:%s, val:%p\n", __FUNCTION__, __LINE__, self, self->parent->name, val);
    if (!strcmp(self->parent->name, "2G")) {
        dm_value_set_string_ext(val, "2G", 1);
    } else {
        dm_value_set_string_ext(val, "5G", 1);
    }
    return 0;
}
static int OperatingFrequencyBand_setter(struct dm_object* self, struct dm_value* val) {
    HR_LOGD("%s(%d): this %p -> parent:%s, val:%p\n", __FUNCTION__, __LINE__, self, self->parent->name, val);

    if (val->type == DM_TYPE_NUMBER) {
        HR_LOGD("%s(%d): this %p -> parent:%s, val:%d\n", __FUNCTION__, __LINE__, self, self->parent->name, val->val.number);
    } else if (val->type == DM_TYPE_STRING) {
        HR_LOGD("%s(%d): this %p -> parent:%s, val:%s\n", __FUNCTION__, __LINE__, self, self->parent->name, val->val.string);
    }

    const char* params[10] = {NULL};

    params[0] = "Hosts.Host.8";
    params[1] = "Hosts.Host.11";
    params[2] = strdup("xxxxxxxxxxx");
    if (!strcmp(self->parent->name, "2G")) {
        params[3] = strdup("Device.WiFi.X_CU_ACL.2G");

    } else {
        params[3] = strdup("Device.WiFi.X_CU_ACL.5G");
    }

    uc_dm_notify(params, sizeof(params) / sizeof(params[0]));

    return 0;
}

static int MACAddressControlEnabled_getter(struct dm_object* self, struct dm_value* val) {
    HR_LOGD("%s(%d): this %p -> parent:%s, val:%p\n", __FUNCTION__, __LINE__, self, self->parent->name, val);
    if (!strcmp(self->parent->name, "2G")) {
        dm_value_set_string_ext(val, "2G", 1);
    } else {
        dm_value_set_string_ext(val, "5G", 1);
    }
    return 0;
}
static int MACAddressControlEnabled_setter(struct dm_object* self, struct dm_value* val) {
    HR_LOGD("%s(%d): this %p -> parent:%s, val:%p\n", __FUNCTION__, __LINE__, self, self->parent->name, val);

    if (val->type == DM_TYPE_NUMBER) {
        HR_LOGD("%s(%d): this %p -> parent:%s, val:%d\n", __FUNCTION__, __LINE__, self, self->parent->name, val->val.number);
    } else if (val->type == DM_TYPE_STRING) {
        HR_LOGD("%s(%d): this %p -> parent:%s, val:%s\n", __FUNCTION__, __LINE__, self, self->parent->name, val->val.string);
    }

    return 0;
}
static int HostName_getter(struct dm_object* self, struct dm_value* val) {
    HR_LOGD("%s(%d): this %p -> parent:%s, val:%p\n", __FUNCTION__, __LINE__, self, self->parent->name, val);
    if (!strcmp(self->parent->name, "2G")) {
        dm_value_set_string_ext(val, "2G.Host", 1);
    } else {
        dm_value_set_string_ext(val, "5G.Host", 1);
    }
    return 0;
}
static int HostName_setter(struct dm_object* self, struct dm_value* val) {
    HR_LOGD("%s(%d): this %p -> parent:%s, val:%p\n", __FUNCTION__, __LINE__, self, self->parent->name, val);

    if (val->type == DM_TYPE_NUMBER) {
        HR_LOGD("%s(%d): this %p -> parent:%s, val:%d\n", __FUNCTION__, __LINE__, self, self->parent->name, val->val.number);
    } else if (val->type == DM_TYPE_STRING) {
        HR_LOGD("%s(%d): this %p -> parent:%s, val:%s\n", __FUNCTION__, __LINE__, self, self->parent->name, val->val.string);
    }

    return 0;
}

static int MACAddress_getter(struct dm_object* self, struct dm_value* val) {
    HR_LOGD("%s(%d): this %p -> parent:%s, val:%p\n", __FUNCTION__, __LINE__, self, self->parent->name, val);
    if (!strcmp(self->parent->name, "2G")) {
        dm_value_set_string_ext(val, "2G.Host", 1);
    } else {
        dm_value_set_string_ext(val, "5G.Host", 1);
    }
    return 0;
}
static int MACAddress_setter(struct dm_object* self, struct dm_value* val) {
    HR_LOGD("%s(%d): this %p -> parent:%s, val:%p\n", __FUNCTION__, __LINE__, self, self->parent->name, val);

    if (val->type == DM_TYPE_NUMBER) {
        HR_LOGD("%s(%d): this %p -> parent:%s, val:%d\n", __FUNCTION__, __LINE__, self, self->parent->name, val->val.number);
    } else if (val->type == DM_TYPE_STRING) {
        HR_LOGD("%s(%d): this %p -> parent:%s, val:%s\n", __FUNCTION__, __LINE__, self, self->parent->name, val->val.string);
    }

    return 0;
}

static int MacFilterPolicy_getter(struct dm_object* self, struct dm_value* val) {
    HR_LOGD("%s(%d): this %p -> parent:%s, val:%p\n", __FUNCTION__, __LINE__, self, self->parent->name, val);
    if (!strcmp(self->parent->name, "2G")) {
        dm_value_set_string_ext(val, "2G.Host", 1);
    } else {
        dm_value_set_string_ext(val, "5G.Host", 1);
    }
    return 0;
}
static int MacFilterPolicy_setter(struct dm_object* self, struct dm_value* val) {
    HR_LOGD("%s(%d): this %p -> parent:%s, val:%p\n", __FUNCTION__, __LINE__, self, self->parent->name, val);

    if (val->type == DM_TYPE_NUMBER) {
        HR_LOGD("%s(%d): this %p -> parent:%s, val:%d\n", __FUNCTION__, __LINE__, self, self->parent->name, val->val.number);
    } else if (val->type == DM_TYPE_STRING) {
        HR_LOGD("%s(%d): this %p -> parent:%s, val:%s\n", __FUNCTION__, __LINE__, self, self->parent->name, val->val.string);
    }

    return 0;
}

static int WMacFilters_setter(struct dm_object* self, struct dm_value* val) {
    HR_LOGD("%s(%d): this %p -> parent:%s, val:%p\n", __FUNCTION__, __LINE__, self, self->parent->name, val);
    HR_LOGD("%s(%d): this %p -> parent:%s, val:%s\n", __FUNCTION__, __LINE__, self, self->parent->name, val->val.string);
    return 0;
}

static int BMacFilters_setter(struct dm_object* self, struct dm_value* val) {
    HR_LOGD("%s(%d): this %p -> parent:%s, val:%p\n", __FUNCTION__, __LINE__, self, self->parent->name, val);
    HR_LOGD("%s(%d): this %p -> parent:%s, val:%s\n", __FUNCTION__, __LINE__, self, self->parent->name, val->val.string);
    return 0;
}

static int X_CU_SavePower_i_setter(struct dm_object* self, struct dm_value* val) {
    HR_LOGD("%s(%d): this %p ->%s  parent:%s, val:%s\n", __FUNCTION__, __LINE__, self, self->name, self->parent->name, val->val.string);
    return 0;
}

static int X_CU_SavePower_adder(struct dm_object* self, struct dm_value* val) {
    static int id = 1;
    char tmp[128] = {0};
    HR_LOGD("%s(%d): this %p ->%s  parent:%s, val:%s\n", __FUNCTION__, __LINE__, self, self->name, self->parent->name, val->val.string);
    snprintf(tmp, sizeof(tmp), "%d", id);
    struct dm_object* new = dm_object_new(tmp, DM_TYPE_OBJECT, dm_object_attribute, X_CU_SavePower_i_setter, self);
    dm_object_new("Enable", DM_TYPE_NUMBER, NULL, NULL, new);
    dm_object_new("EndTime", DM_TYPE_STRING, NULL, NULL, new);
    dm_object_new("StartTime", DM_TYPE_STRING, NULL, NULL, new);
    dm_object_new("RepeatDay", DM_TYPE_STRING, NULL, NULL, new);

    dm_object_id(new, tmp, sizeof(tmp));
    dm_value_set_string(val, tmp);

    HR_LOGD("%s(%d): new object:%s\n", __FUNCTION__, __LINE__, tmp);
    id++;
    return 0;
}

static int X_CU_SavePower_deleter(struct dm_object* self, struct dm_value* val) {
    HR_LOGD("%s(%d): this %p ->%s  parent:%s, val:%s\n", __FUNCTION__, __LINE__, self, self->name, self->parent->name, val->val.string);

    struct dm_object* del = dm_object_lookup(val->val.string, NULL);

    // do system related work

    // finally release resource
    dm_object_free(del);

    return 0;
}

int dm_Device_WiFi(struct dm_object* parent) {
    struct dm_object* WiFi = dm_object_new("WiFi", DM_TYPE_OBJECT, dm_object_attribute, NULL, parent);

    struct dm_object* X_CU_ACL = dm_object_new("X_CU_ACL", DM_TYPE_OBJECT, dm_object_attribute, NULL, WiFi);

    struct dm_object* wifi_2g = dm_object_new("2G", DM_TYPE_OBJECT, dm_object_attribute, NULL, X_CU_ACL);
    struct dm_object* wifi_5g = dm_object_new("5G", DM_TYPE_OBJECT, dm_object_attribute, NULL, X_CU_ACL);

    dm_object_new("OperatingFrequencyBand", DM_TYPE_STRING, OperatingFrequencyBand_getter, OperatingFrequencyBand_setter, wifi_2g);
    dm_object_new("OperatingFrequencyBand", DM_TYPE_STRING, OperatingFrequencyBand_getter, OperatingFrequencyBand_setter, wifi_5g);

    dm_object_new("MACAddressControlEnabled", DM_TYPE_NUMBER, MACAddressControlEnabled_getter, MACAddressControlEnabled_setter, wifi_2g);
    dm_object_new("MACAddressControlEnabled", DM_TYPE_NUMBER, MACAddressControlEnabled_getter, MACAddressControlEnabled_setter, wifi_5g);

    dm_object_new("MacFilterPolicy", DM_TYPE_NUMBER, MacFilterPolicy_getter, MacFilterPolicy_setter, wifi_2g);
    dm_object_new("MacFilterPolicy", DM_TYPE_NUMBER, MacFilterPolicy_getter, MacFilterPolicy_setter, wifi_5g);

#if 1
    struct dm_object* wifi_2g_WMacFilters = dm_object_new("WMacFilters", DM_TYPE_OBJECT, dm_object_attribute, WMacFilters_setter, wifi_2g);
    struct dm_object* wifi_5g_WMacFilters = dm_object_new("WMacFilters", DM_TYPE_OBJECT, dm_object_attribute, WMacFilters_setter, wifi_5g);
    struct dm_object* wifi_2g_BMacFilters = dm_object_new("BMacFilters", DM_TYPE_OBJECT, dm_object_attribute, BMacFilters_setter, wifi_2g);
    struct dm_object* wifi_5g_BMacFilters = dm_object_new("BMacFilters", DM_TYPE_OBJECT, dm_object_attribute, BMacFilters_setter, wifi_5g);

    dm_object_new("MACAddress", DM_TYPE_STRING, MACAddress_getter, MACAddress_setter, wifi_2g_WMacFilters);
    dm_object_new("HostName", DM_TYPE_STRING, HostName_getter, HostName_setter, wifi_2g_WMacFilters);

    dm_object_new("MACAddress", DM_TYPE_STRING, MACAddress_getter, MACAddress_setter, wifi_2g_BMacFilters);
    dm_object_new("HostName", DM_TYPE_STRING, HostName_getter, HostName_setter, wifi_2g_BMacFilters);

    dm_object_new("MACAddress", DM_TYPE_STRING, MACAddress_getter, MACAddress_setter, wifi_5g_WMacFilters);
    dm_object_new("HostName", DM_TYPE_STRING, HostName_getter, HostName_setter, wifi_5g_WMacFilters);

    dm_object_new("MACAddress", DM_TYPE_STRING, MACAddress_getter, MACAddress_setter, wifi_5g_BMacFilters);
    dm_object_new("HostName", DM_TYPE_STRING, HostName_getter, HostName_setter, wifi_5g_BMacFilters);
#else

    dm_object_new("WMacFilters", DM_TYPE_OBJECT, dm_object_attribute, WMacFilters_setter, wifi_2g);
    dm_object_new("WMacFilters", DM_TYPE_OBJECT, dm_object_attribute, WMacFilters_setter, wifi_5g);
    dm_object_new("BMacFilters", DM_TYPE_OBJECT, dm_object_attribute, BMacFilters_setter, wifi_2g);
    dm_object_new("BMacFilters", DM_TYPE_OBJECT, dm_object_attribute, BMacFilters_setter, wifi_5g);

#endif

    struct dm_object* X_CU_SavePower = dm_object_new("X_CU_SavePower", DM_TYPE_OBJECT, dm_object_attribute, NULL /*X_CU_SavePower_setter*/, WiFi);
    X_CU_SavePower->adder = X_CU_SavePower_adder;
    X_CU_SavePower->deleter = X_CU_SavePower_deleter;

    char tmp[128] = {0};

    dm_object_id(wifi_5g_WMacFilters, tmp, sizeof(tmp));

    HR_LOGD("%s(%d): objectid :%s\n", __FUNCTION__, __LINE__, tmp);

    // int *ptr = 0;
    // *ptr = 0;
    return 0;
}