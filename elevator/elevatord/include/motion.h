#ifndef MOTION_H
#define MOTION_H
#include <stddef.h>
#include <stdint.h>
#include "sensor.h"

enum motion_state { STOPPED,
                    ACCELERATING,
                    DECELERATING,
                    CONSTANTING };

enum motion_observer_action {
    MOTION_OBSERVER_ACTION_ON_STATUS,
    MOTION_OBSERVER_ACTION_ON_EVENT,
    MOTION_OBSERVER_ACTION_ON_SENSOR_CALIBRATION
};

enum motion_direction {
    DIRECTION_NONE = 0,
    DIRECTION_UP,
    DIRECTION_DOWN,
};

struct motion_status{
    double accel;
    double velocity;
    double distance;
    double height;
    double jitter_accel;
    double jitter_frequency;
    int floor;
    int running;
    double pressure;
    double temperature;
    double barometer_velocity;
    double barometer_distance;
};

struct motion_event {
    enum motion_state state;
    enum motion_direction direction;
    int floor;
    int floor_begin;
    double height;
    double distance;
    double pressure;
    double temperature;
    int64_t timestamp_begin;
    int64_t timestamp_end;
};
#define CALIBRATION_EVENT_VALUES_MAX 10

struct motion_sensor_calibration_event {
    enum sensor_type type;
    int state;
    double value[CALIBRATION_EVENT_VALUES_MAX];
};

struct motion_observer{
    void (*on_status) (struct motion_status *stat);
    void (*on_event) (struct motion_event* data);
    void (*on_sensor_calibration) (struct motion_sensor_calibration_event* data);
};


int motion_initalize(int argc, char** argv);
int motion_deinitalize(void);

int motion_register_observer(struct motion_observer *observer);
int motion_unregister_observer(struct motion_observer* observer);
int motion_run(void);

int motion_calibrate_at_floor(int floor);
int motion_calibrate_at_height(double height);
// imu calibration
int motion_enter_sensor_calibration();
#endif