#include "dm_lift.h"

int dm_lift_init(void) {
    return 0;
}
const char* dm_lift_id(void) {
    return "NO.123";
}
int dm_lift_direction(void) {
    return LF_DIRECTION_STATIONARY;
}
int dm_lift_floor(void) {
    return 10;
}
float dm_lift_speed(void) {
    return 0.5f;
}
float dm_lift_travel_distance(void) {
    return  10.f;
}
int dm_lift_door(void) {
    return 1;
}
int dm_lift_passenger_count(void) {
    return 6;
}
int dm_lift_passenger_count_in(void) {
    return 2;
}
int dm_lift_passenger_count_out(void) {
    return 1;
}

float dm_lift_temperature(void) {
    return 24.2f;
}
