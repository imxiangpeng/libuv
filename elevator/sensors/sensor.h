#ifndef SENSOR_H
#define SENSOR_H

#define IMU_AXES 3

enum sensor {
    SENSOR_ACCELEROMETER,
    SENSOR_GYROSCOPE,
    SENSOR_BAROMETER,
    SENSOR_TEMPERATURE,
    _SENSOR_MAX
};

struct sensor_data {
    enum sensor type;
};

struct sensor_data_accelerometer {
    struct sensor_data self;
    double x[IMU_AXES];
    
#if USE_LOCAL_SIMULATE_DATA    
    double dt;
#endif    
};

struct sensor_data_gyroscope {
    struct sensor_data self;
    double x[IMU_AXES];
};

struct sensor_data_barometer{
    struct sensor_data self;
    double pressure;
    double temperature;
};

struct sensor_data_temperature {
    struct sensor_data self;
    double temperature;
};




struct sensor_device {
    int (*init)();
    int (*configure)(int sampling_rate);
    int (*read)(struct sensor_data* data);
    int (*close)();
};

int sensor_manager_init();

struct sensor_device* sensor_manager_get_device(enum sensor type);
#endif
