#ifndef MOTION_H
#define MOTION_H
#include <stddef.h>

enum motion_state { STOPPED,
                    ACCELERATING,
                    DECELERATING,
                    CONSTANTING };

enum motion_observer_action {
    MOTION_OBSERVER_ACTION_ON_STATUS,
    MOTION_OBSERVER_ACTION_ON_EVENT
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


struct motion_observer{
    void (*on_status) (struct motion_status *stat);
    void (*on_event) (struct motion_event* data);
};


int motion_initalize(int argc, char** argv);
int motion_register_observer(struct motion_observer *observer);
int motion_run(void);

#endif