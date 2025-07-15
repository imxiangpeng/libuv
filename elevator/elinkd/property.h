#ifndef PROPERTY_H
#define PROPERTY_H
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
enum property_type {
    E_UNKNOWN = 0,
    E_NUMBER, // use int64
    E_DECIMAL,
    E_STRING,
    E_BOOLEAN,
};

struct property_value {
    enum property_type type;
    union {
        int64_t number;
        double decimal;
        const char* string;
        bool boolean;
    } val;
    int preallocated;  // union value is preallocated, you no need free it...
};

struct property {
    const char* name;
    enum property_type type;
    // some property no need get
    // dispatch when property is changed
    // there update value directly and mark dirty
    int (*getter)(struct property*self);
    int (*setter)(struct property*self, struct property_value *value);
    struct property_value value;
    int dirty;
};

struct property* property_get(const char* name);


int property_value_reset(struct property_value*);
int property_value_set_number(struct property_value*, int64_t);
int property_value_set_decimal(struct property_value*, double);
int property_value_set_boolean(struct property_value*, int);
int property_value_set_string(struct property_value*, const char*);
int property_value_set_string_ext(struct property_value*, const char*, int preallocated);

enum {
    PROPERTY_SERIALNO = 0,
    PROPERTY_ELEVATOR_ID,
    PROPERTY_BUILD_TIMESTAMP,
    PROPERTY_SW_VERSION,
    PROPERTY_BIAS_ACCEL_X,
    PROPERTY_BIAS_ACCEL_Y,
    PROPERTY_BIAS_ACCEL_Z,
    PROPERTY_BIAS_PITCH,
    PROPERTY_BIAS_ROLL,
    PROPERTY_IMU_CALIBRATION,
    PROPERTY_FLOOR,
    PROPERTY_HEIGHT,
    PROPERTY_PRESSURE,
    PROPERTY_TEMPERATURE,
    PROPERTY_FLOOR_MODEL,
    PROPERTY_REPORT_SWITCH,
    PROPERTY_HQLIFTD_CONFIG,
    PROPERTY_EGUARD_ALARM_SWITCH,
    PROPERTY_EGUARD_DTOF_SWITCH,
    PROPERTY_EGUARD_DTOF_OCCLUSION_DISTANCE,
    PROPERTY_DOOR_ROI,
    __PROPERTY_MAX
};

extern struct property properties_tbl[__PROPERTY_MAX];

#endif