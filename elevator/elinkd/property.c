#include <stdlib.h>
#include <string.h>

#include "elevator.h"
#include "property.h"
#include "system.h"

int property_value_reset(struct property_value* val) {
    if (!val) return 0;

    switch (val->type) {
        case E_STRING: {
            if (val->val.string) {
                if (val->preallocated == 0)
                    free((void*)val->val.string);
                val->val.string = NULL;
            }
            val->preallocated = 0;
            break;
        }
        default:
            break;
    }

    return 0;
}
int property_value_set_number(struct property_value* prop, int64_t number) {
    if (!prop) return -1;

    property_value_reset(prop);
    prop->type = E_NUMBER;
    prop->val.number = number;
    return 0;
}

int property_value_set_boolean(struct property_value* prop, int value) {
    if (!prop) return -1;

    property_value_reset(prop);

    prop->type = E_BOOLEAN;
    prop->val.boolean = !!value;
    return 0;
}
int property_value_set_string(struct property_value* prop, const char* str) {
    if (!prop || !str) return -1;

    property_value_reset(prop);

    prop->type = E_STRING;
    prop->val.string = strdup(str);
    prop->preallocated = 0;

    return 0;
}
int property_value_set_string_ext(struct property_value* prop, const char* str, int preallocated) {
    if (!prop || !str) return -1;

    property_value_reset(prop);

    prop->type = E_STRING;

    if (preallocated == 0)
        prop->val.string = strdup(str);
    else
        prop->val.string = str;

    prop->preallocated = preallocated;

    return 0;
}

struct property properties_tbl[] = {
    // system property_value
    [PROPERTY_BUILD_TIMESTAMP] = {"build_timestamp", E_STRING, system_property_build_timestamp, NULL, 1 /* report when startup*/},
    [PROPERTY_SW_VERSION] = {"sw_version", E_STRING, system_property_sw_version, NULL, 1 /* report when startup*/},
    // elevator property_value
    [PROPERTY_ELEVATOR_ID] = {"elevator_id", E_STRING, elevator_property_elevator_id, NULL, 1 /* report when startup*/},
    // camera property_value
};

size_t properties_tbl_size = sizeof(properties_tbl)/sizeof(properties_tbl[0]);