#ifndef MOTION_H
#define MOTION_H
#include <stddef.h>
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

struct motion_status{
    double accel;
    double speed;
    double distance;
    double height;
    int floor;
    int running;
    double pressure;
    double barometer_velocity;
    double barometer_distance;
};

struct motion_event {
    enum motion_state state;
    double distance;
    double pressure;
};
#define CALIBRATION_EVENT_VALUES_MAX 5

struct motion_sensor_calibration_event {
    enum sensor_type type;
    int is_calibration;
    double value[CALIBRATION_EVENT_VALUES_MAX];
};

struct motion_observer{
    void (*on_status) (struct motion_status *stat);
    void (*on_event) (struct motion_event* data);
    void (*on_sensor_calibration) (struct motion_sensor_calibration_event* data);
};


int motion_initalize(int argc, char** argv);
int motion_register_observer(struct motion_observer *observer);
int motion_unregister_observer(struct motion_observer* observer);
int motion_run(void);

int motion_calibrate_at_floor(int floor);
int motion_calibrate_at_height(double height);
// imu calibration
int motion_enter_sensor_calibration();
#endif