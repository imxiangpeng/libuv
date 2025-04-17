int core_initalize(int argc, char** argv);
int core_run(void);

enum motion_state { STOPPED,
                    ACCELERATING,
                    DECELERATING,
                    CONSTANTING };

enum observer_action {
    OBSERVER_ACTION_ON_STATUS,
    OBSERVER_ACTION_ON_MOTION
};
struct status_data{
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
struct core_observer{
    // void (*update) (enum core_sensor, void *data);
    void (*on_status) (struct status_data *stat);
    void (*on_motion) (struct motion_data* data);
};

int core_register_observer(struct core_observer *observer);
