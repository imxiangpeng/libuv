#ifndef SENSOR_H
#define SENSOR_H

enum sensor {
    SENSOR_ACCELERATION,
    SENSOR_GYRO,
    SENSOR_BARO,
    _SENSOR_MAX
};

struct sensor_data {
    enum sensor type;
};

struct sensor_data_accelerator {
    struct sensor_data self;
    double x;
    double y;
    double z;
};

struct sensor_data_gyro {
    struct sensor_data self;
    double x;
    double y;
    double z;
};

struct sensor_data_baro{
    struct sensor_data self;
    double x;
    double y;
    double z;
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

#endif