int core_initalize(int argc, char** argv);
int core_run(void);
enum core_sensor {
    SENSOR_ACCELERATION = 0,
    SENSOR_PRESSURE,
    _SENSOR_MAX
};

struct core_observer{
    void (*update) (enum core_sensor, void *data);
};

int core_register_observer(enum core_sensor type, struct core_observer *observer);