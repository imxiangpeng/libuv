
#ifndef ELEVATOR_H
#define ELEVATOR_H
enum elevator_direction {
    ELEVATOR_DIR_STATIONARY = 0,
    ELEVATOR_DIR_UP, // 1
    ELEVATOR_DIR_DOWN // 2
};

enum elevator_door_state{
    ELEVATROR_DOOR_CLOSE = 0,
    ELEVATOR_DOOR_OPEN
};

struct elevator_status {
    enum elevator_direction direction;
    enum elevator_door_state door_state;
    double accel;
    double speed;
    double distance;
    int current_floor;
    int passenger_count;
};

int elevator_init(void);
const char* elevator_deviceid(void);
int elevator_get_status(struct elevator_status *st);
int elevator_direction(void);
int elevator_floor(void);
float elevator_speed(void);
float elevator_travel_distance(void);
int elevator_door(void);
int elevator_passenger_count(void);
int elevator_passenger_count_in(void);
int elevator_passenger_count_out(void);
float elevator_temperature(void);

#endif