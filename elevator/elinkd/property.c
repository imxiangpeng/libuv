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
    [PROPERTY_SERIALNO] = {"serialno", E_STRING, system_property_serial, NULL, {}, 1 /* report when startup*/},
    [PROPERTY_ELEVATOR_ID] = {"elevator_id", E_STRING, elevator_property_get_elevator_id, elevator_property_set_elevator_id, {}, 1 /* report when startup*/},
    [PROPERTY_BUILD_TIMESTAMP] = {"build_timestamp", E_STRING, NULL, NULL, {.type = E_STRING, .val.string = BUILD_TIMESTAMP, .preallocated = 1}, 1 /* report when startup*/},
    [PROPERTY_SW_VERSION] = {"sw_version", E_STRING, system_property_sw_version, NULL, {}, 1 /* report when startup*/},
    // elevator property_value
    // calibration
    [PROPERTY_BIAS_ACCEL_X] = {"bias_accel_x", E_DECIMAL, NULL, NULL, {}, 0},
    [PROPERTY_BIAS_ACCEL_Y] = {"bias_accel_y", E_DECIMAL, NULL, NULL, {}, 0},
    [PROPERTY_BIAS_ACCEL_Z] = {"bias_accel_z", E_DECIMAL, NULL, NULL, {}, 0},
    [PROPERTY_BIAS_PITCH] = {"bias_pitch", E_DECIMAL, NULL, NULL, {}, 0},
    [PROPERTY_BIAS_ROLL] = {"bias_roll", E_DECIMAL, NULL, NULL, {}, 0},
    [PROPERTY_IMU_CALIBRATION] = {"imu_calibration", E_DECIMAL, NULL, elevator_property_enter_sensor_calibration, {}, 0},
    // elevator
    // floor/height/pressure/temperature only report when low level report
    [PROPERTY_FLOOR] = {"floor", E_NUMBER, NULL, NULL, {E_NUMBER, .val.number = 0, .preallocated = 0}, 0},
    [PROPERTY_HEIGHT] = {"height", E_DECIMAL, NULL, NULL, {E_DECIMAL, .val.decimal = 0, 0}, 0},
    [PROPERTY_PRESSURE] = {"pressure", E_DECIMAL, NULL, NULL, {E_DECIMAL, .val.decimal = 0, 0}, 0},
    [PROPERTY_TEMPERATURE] = {"temperature", E_DECIMAL, NULL, NULL, {E_DECIMAL, .val.decimal = 0, 0}, 0},
    [PROPERTY_FLOOR_MODEL] = {"floor_model", E_STRING, elevator_property_get_floor_model, NULL, {}, 0},
    // hqliftd
    [PROPERTY_HQLIFTD_CONFIG] = {"hqliftd_config", E_STRING, elevator_property_get_hqliftd_config, elevator_property_set_hqliftd_config, {}, 0},
    // eguard
    [PROPERTY_EGUARD_ALARM_SWITCH] = {"eguard_alarm_switch", E_NUMBER, elevator_property_get_eguard_alarm_switch, elevator_property_set_eguard_alarm_switch, {}, 1},
    [PROPERTY_EGUARD_ALARM_INTERVAL] = {"eguard_alarm_interval", E_NUMBER, elevator_property_get_eguard_alarm_interval, elevator_property_set_eguard_alarm_interval, {}, 1},
    [PROPERTY_EGUARD_ALARM_REPEAT_COUNT] = {"eguard_alarm_repeat_count", E_NUMBER, elevator_property_get_eguard_alarm_repeat_count, elevator_property_set_eguard_alarm_repeat_count, {}, 0},
    [PROPERTY_EGUARD_DTOF_SWITCH] = {"eguard_dtof_switch", E_NUMBER, elevator_property_get_eguard_dtof_switch, elevator_property_set_eguard_dtof_switch, {}, 0},
    [PROPERTY_EGUARD_DTOF_OCCLUSION_DISTANCE] = {"eguard_dtof_occlusion_distance", E_NUMBER, elevator_property_get_eguard_dtof_occlusion_distance, elevator_property_set_eguard_dtof_occlusion_distance, {}, 0},
    [PROPERTY_EGUARD_KUNREN_DETECT_ENABLED] = {"eguard_kunren_detect_enabled", E_NUMBER, elevator_property_get_eguard_kunren_detect_enabled, elevator_property_set_eguard_kunren_detect_enabled, {}, 1},
    [PROPERTY_EGUARD_KUNREN_DETECT_TIMEOUT] = {"eguard_kunren_detect_timeout", E_NUMBER, elevator_property_get_eguard_kunren_detect_timeout, elevator_property_set_eguard_kunren_detect_timeout, {}, 1},
    [PROPERTY_EGUARD_KUNREN_ALARM_REPEAT_COUNT] = {"eguard_kunren_alarm_repeat_count", E_NUMBER, elevator_property_get_eguard_kunren_alarm_repeat_count, elevator_property_set_eguard_kunren_alarm_repeat_count, {}, 0},
    [PROPERTY_EGUARD_DOOR_ZONE_STOPPED_THRESHOLD] = {"eguard_door_zone_stopped_threshold", E_DECIMAL, elevator_property_get_eguard_door_zone_stopped_threshold, elevator_property_set_eguard_door_zone_stopped_threshold, {}, 1},
    [PROPERTY_EGUARD_DOOR_CONTROL_ENABLED] = {"eguard_door_control_enabled", E_NUMBER, elevator_property_get_eguard_door_control_enabled, elevator_property_set_eguard_door_control_enabled, {}, 1},
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