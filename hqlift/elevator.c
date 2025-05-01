#include "elevator.h"
#include <stdio.h>

#include "platform.h"

static char _device_id[256] = {0};
int elevator_init(void) {
    platform_get_property(PROPERTY_DEVICEID, _device_id, sizeof(_device_id));
    return 0;
}
const char* elevator_deviceid(void) {
    // return _device_id;
    return "NO.123";
}
int elevator_get_status(struct elevator_status *st) {
    if (!st) {
        return -1;
    }
    

    return 0;
}
int elevator_direction(void) {
    return ELEVATOR_DIR_STATIONARY;
}
int elevator_floor(void) {
    return 10;
}
float elevator_speed(void) {
    return 0.5f;
}
float elevator_travel_distance(void) {
    return  10.f;
}
int elevator_door(void) {
    return 1;
}
int elevator_passenger_count(void) {
    return 6;
}
int elevator_passenger_count_in(void) {
    return 2;
}
int elevator_passenger_count_out(void) {
    return 1;
}

float elevator_temperature(void) {
    float temp = 15.0;
    const char* temp_channel = "/sys/bus/iio/devices/iio:device0/in_temp_input";
    FILE *fp = fopen(temp_channel, "r");
    if (fp == NULL) {
        return 15.0f;
    }

    if (fscanf(fp, "%f", &temp) != 1) {
        fclose(fp);
        return temp;
    }

    fclose(fp);
    return temp;
}
