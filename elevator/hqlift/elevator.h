
#ifndef ELEVATOR_H
#define ELEVATOR_H
#include <stdint.h>
#include "hr_buffer.h"

enum elevator_direction {
    ELEVATOR_DIR_STATIONARY = 0,
    ELEVATOR_DIR_UP, // 1
    ELEVATOR_DIR_DOWN // 2
};

enum elevator_door_state{
    ELEVATOR_DOOR_OPEN = 0,
    ELEVATROR_DOOR_CLOSE
};

struct elevator_status {
    enum elevator_direction direction;
    enum elevator_door_state door_state;
    double accel;
    double speed;
    double distance;
    int current_floor;
    int passenger_count;
    double jitter_freq;
    double jitter_accel;
};

struct elevator_historical {
    double distance;
    int direction;
    uint64_t timestamp_begin;
    uint64_t timestamp_end;
    int floor_begin;
    int floor_end;
    struct hrbuffer accel_array;
    struct hrbuffer speed_array;
    struct hrbuffer jitter_accel_array;
    struct hrbuffer jitter_frequency_array;
};

int elevator_init(void);

const char* elevator_serialno(void);
const char* elevator_mac(void);
const char* elevator_deviceid(void);
int elevator_get_status(struct elevator_status *st);

int elevator_passenger_count(void);
int elevator_passenger_count_in(void);
int elevator_passenger_count_out(void);
double elevator_temperature(void);

int elevator_light_brightness();
#endif