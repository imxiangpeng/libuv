
#ifndef ELEVATOR_H
#define ELEVATOR_H
#include <stdint.h>
#include "hr_buffer.h"

// #define HQLIFTD_CONF_PATH "/etc/hqliftd/hqliftd.conf"
// MQ_SERVER=mq.hqszjs.com
// MQ_PORT=1883
// MQ_KEEPALIVE=60
// MQ_USERNAME=inspur
// MQ_PASSWORD=inspur88*
// REALTIME_REPORT_PERIOD_MS=1000
// SPEED_LIMIT_THREHOLD=3.1
// FTP_ADDRESS=
// FTP_USERNAME=
// FTP_PASSWORD=
// #define HQLIFTD_CONF_PATH "hqliftd.conf"


enum elevator_direction {
    ELEVATOR_DIR_STATIONARY = 0,
    ELEVATOR_DIR_UP,   // 1
    ELEVATOR_DIR_DOWN  // 2
};

enum elevator_door_state {
    ELEVATOR_DOOR_OPEN = 0,
    ELEVATOR_DOOR_CLOSE
};

// match houqi value
enum elevator_exception {
    ELEVATOR_EXCEPTION_NONE = 0,
    // 1. 困人
    ELEVATOR_EXCEPTION_PEOPLE_TRAPPED = 1 << 0,
    // 2. 开门走车
    ELEVATOR_EXCEPTION_RUN_WITHOUT_DOOR_CLOSED = 1 << 1,
    // 3. 非门区停车
    ELEVATOR_EXCEPTION_STOPPED_NOT_AT_DOOR = 1 << 3,
    // 4. 冲顶
    ELEVATOR_EXCEPTION_RUN_OVER_TOP = 1 << 4,
    // 5. 蹲底
    ELEVATOR_EXCEPTION_RUN_OVER_BOTTOM = 1 << 5,
    // 6. 超速
    ELEVATOR_EXCEPTION_OVERSPEED = 1 << 6,
    // 7. 检修

    // 8. 外设故障
    // 9. 电梯停电
    // 10. 电池电量低

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
int elevator_get_status(struct elevator_status* st);

int elevator_passenger_count(void);
int elevator_passenger_count_in(void);
int elevator_passenger_count_out(void);
double elevator_temperature(void);

int elevator_light_brightness();

// it's implement in houqi_liftfault

int elevator_fault_occurred(enum elevator_exception fault);
int elevator_fault_resolved(enum elevator_exception fault);

int elevator_fault_review(int *type, uint64_t *occurred_ms);

#endif
