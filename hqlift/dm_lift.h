
#ifndef DM_LIFT_H
#define DM_LIFT_H
enum {
    LF_DIRECTION_STATIONARY = 0,
    LF_DIRECTION_UP, // 1
    LF_DIRECTION_DOWN // 2
};

enum {
    LF_DOOR_CLOSE,
    LF_DOOR_OPEN
};

int dm_lift_init(void);
const char* dm_lift_id(void);
int dm_lift_direction(void);
int dm_lift_floor(void);
float dm_lift_speed(void);
float dm_lift_travel_distance(void);
int dm_lift_door(void);
int dm_lift_passenger_count(void);
int dm_lift_passenger_count_in(void);
int dm_lift_passenger_count_out(void);

float dm_lift_temperature(void);

#endif