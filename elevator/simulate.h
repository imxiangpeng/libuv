#ifndef SIMULATE_H
#define SIMULATE_H
struct simulate_data {

    double now;
    double accel_x;
    double accel_y;
    double accel_z;
    double gyro_x;
    double gyro_y;
    double gyro_z;
    double pressure;
    double temp;
};
int simulate_data_init(void);
int simulate_data_read(struct simulate_data *data);
int simulate_data_deinit(void);
#endif