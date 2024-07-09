#include "dm_Reboot.h"

#include "hr_log.h"

static int Reboot_setter(struct dm_object* self, struct dm_value* val) {
    HR_LOGD("%s(%d): this %p, val:%p\n", __FUNCTION__, __LINE__, self, val);

    if (val->type == DM_TYPE_STRING)
        HR_LOGD("%s(%d): this %s, val:%s\n", __FUNCTION__, __LINE__, self->name, val->val.string);

    return 0;
}

int dm_Reboot_init(struct dm_object* parent) {
    dm_object_new("Reboot", DM_TYPE_OBJECT, dm_object_attribute, Reboot_setter, parent);
    return 0;
}
