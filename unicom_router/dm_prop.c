
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "dm_object.h"
#include "hr_log.h"



int main(int argc, char** argv) {

    dm_object_new_ext("Device.DeviceInfo.ModelName2", DM_TYPE_STRING,
                      NULL, NULL);
    struct dm_object *Device = dm_object_lookup("Device", NULL);

    struct dm_object *object = dm_object_lookup("Device.X_CU_LockEnable", NULL);
    if (object) {
        struct dm_value val;
        memset((void *)&val, 0, sizeof(val));
        object->getter(object, &val);
        HR_LOGD("lock:%d\n", val.val.number);
        // val.val.number = !val.val.number;
        // val.val.boolean= !val.val.boolean;
        HR_LOGD("adjust lock:%d\n", val.val.number);
        object->setter(object, &val);
        dm_value_reset(&val);
    }

    object = dm_object_lookup("Device.", NULL);
    if (object) {
        struct dm_value val;
        memset((void *)&val, 0, sizeof(val));
        if (object->getter)
            object->getter(object, &val);
        HR_LOGD("Device.:%s\n", val.val.string);
        dm_value_reset(&val);
    }


    object = dm_boolean_new("persist.sys.secure.boot");
    struct dm_value val;
    memset((void*)&val, 0, sizeof(val));
    dm_value_set_boolean(&val, 0);
    printf("o:%p, object->setter:%p\n", object, object->setter);
    object->setter(object, &val);

    object = dm_number_new("persist.sys.secure.success");
    dm_value_set_number(&val, 0);
    object->setter(object, &val);

    object = dm_string_new("persist.sys.build_information");
    dm_value_set_string_ext(&val, "this is test string", 1);
    object->setter(object, &val);
    
    object = dm_object_lookup("persist.", NULL);
    if (object) {
        struct dm_value val;
        memset((void *)&val, 0, sizeof(val));
        if (object->getter)
            object->getter(object, &val);
        HR_LOGD("persist.:%s\n", val.val.string);
        dm_value_reset(&val);
    }

    return 0;
}