// mxp, 20250710, implement elevator related iot property
#include "property.h"

#include <stdlib.h>
#include <string.h>

#include "elevator.h"
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

int property_value_set_decimal(struct property_value* prop, double number) {
    if (!prop) return -1;

    property_value_reset(prop);
    prop->type = E_DECIMAL;
    prop->val.decimal = number;
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

struct property properties_tbl[__PROPERTY_MAX] = {
    // system property_value
    [PROPERTY_BUILD_TIMESTAMP] = {"build_timestamp", E_STRING, NULL, NULL, {.type = E_STRING, .val.string = BUILD_TIMESTAMP, .preallocated = 1}, 1 /* report when startup*/},
    [PROPERTY_SW_VERSION] = {"sw_version", E_STRING, system_property_sw_version, NULL, {}, 1 /* report when startup*/},
    // elevator property_value
    [PROPERTY_ELEVATOR_ID] = {"elevator_id", E_STRING, elevator_property_elevator_id, NULL, {}, 1 /* report when startup*/},

    // calibration
    [PROPERTY_BIAS_ACCEL_X] = {"bias_accel_x", E_DECIMAL, NULL, NULL, {}, 0},
    [PROPERTY_BIAS_ACCEL_Y] = {"bias_accel_y", E_DECIMAL, NULL, NULL, {}, 0},
    [PROPERTY_BIAS_ACCEL_Z] = {"bias_accel_z", E_DECIMAL, NULL, NULL, {}, 0},
    [PROPERTY_BIAS_PITCH] = {"bias_pitch", E_DECIMAL, NULL, NULL, {}, 0},
    [PROPERTY_BIAS_ROLL] = {"bias_roll", E_DECIMAL, NULL, NULL, {}, 0},
    [PROPERTY_IMU_CALIBRATION] = {"imu_calibration", E_DECIMAL, NULL, elevator_property_enter_sensor_calibration, {}, 0},
    // hqliftd
    [PROPERTY_HQLIFTD_CONFIG] = {"hqliftd_config", E_STRING, elevator_property_get_hqliftd_config, elevator_property_set_hqliftd_config, {}, 0 },
    // camera property_value
};

struct property* property_get(const char* name) {
    if (!name) return NULL;

    for (int i = 0; i < __PROPERTY_MAX; i++) {
        if (!properties_tbl[i].name) {
            continue;
        }
        if (0 == strcmp(properties_tbl[i].name, name)) {
            return &properties_tbl[i];
        }
    }
    
    return NULL;
}