#include "dm_IPC.h"

#include "dm_object.h"
#include "hr_log.h"

static int reboot_setter(struct dm_object* self, struct dm_value* val) {
    HR_LOGD("%s(%d): this %p, val:%p\n", __FUNCTION__, __LINE__, self, val);

    if (val->type == DM_TYPE_STRING)
        HR_LOGD("%s(%d): this %s, val:%s\n", __FUNCTION__, __LINE__, self->name, val->val.string);

    return 0;
}

static int reset_setter(struct dm_object* self, struct dm_value* val) {
    HR_LOGD("%s(%d): this %p, val:%p\n", __FUNCTION__, __LINE__, self, val);

    if (val->type == DM_TYPE_STRING)
        HR_LOGD("%s(%d): this %s, val:%s\n", __FUNCTION__, __LINE__, self->name, val->val.string);

    return 0;
}


int dm_IPC_init(struct dm_object* parent) {
    struct dm_object* IPC = dm_object_new("IPC", DM_TYPE_OBJECT, dm_object_attribute, NULL, parent);
    dm_object_new("reboot", DM_TYPE_NUMBER, NULL, reboot_setter, IPC);
    dm_object_new("reset", DM_TYPE_NUMBER, NULL, reset_setter, IPC);
    return 0;
}
