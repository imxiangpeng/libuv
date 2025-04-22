#ifndef MOTION_H
#define MOTION_H
#include <stddef.h>

enum motion_state { STOPPED,
                    ACCELERATING,
                    DECELERATING,
                    CONSTANTING };

enum motion_observer_action {
    MOTION_OBSERVER_ACTION_ON_STATUS,
    MOTION_OBSERVER_ACTION_ON_MOTION
};


struct motion {
    int (*enter_calibration)(struct motion*);
    int (*calibration_completed)(struct motion*);
    int (*runonce)(struct motion*, void*, size_t);
    // only reset velocity/distance
    int (*reset)(struct motion*);
    int (*close)(struct motion*);
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

struct motion_data {
    enum motion_state state;
    double distance;
    double pressure;
};


struct motion_observer{
    void (*on_status) (struct motion_status *stat);
    void (*on_motion) (struct motion_data* data);
};


int motion_initalize(int argc, char** argv);
int motion_run(void);
int motion_register_observer(struct motion_observer *observer);

#endif