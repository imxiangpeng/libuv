#include "elevator.h"

int elevator_init(void) {
    return 0;
}
const char* elevator_deviceid(void) {
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
    return 24.2f;
}
