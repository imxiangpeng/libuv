
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


enum rescue_mode {
    RESCUE_MODE_AUTO = 1,
    RESCUE_MODE_MANUAL = 2,
};

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

    // 11. 反复开关门
    ELEVATOR_EXCEPTION_DOOR_REPEATED = 1 << 11,
    // 12. 关门异常
    ELEVATOR_EXCEPTION_DOOR_CLOSE_ERROR = 1 << 12,
    // 202. 电瓶车
    ELEVATOR_EXCEPTION_EBIKE = 1 << 20,
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

static const char* fault_to_string(enum elevator_exception fault) {

    switch (fault) {
        // 1. 困人
        case ELEVATOR_EXCEPTION_PEOPLE_TRAPPED:
            return "kunren";
        // 2. 开门走车
        case ELEVATOR_EXCEPTION_RUN_WITHOUT_DOOR_CLOSED:
            return "kaimenzouche";
        // 3. 非门区停车
        case ELEVATOR_EXCEPTION_STOPPED_NOT_AT_DOOR:
            return "feimenqutingche";
        // 4. 冲顶
        case ELEVATOR_EXCEPTION_RUN_OVER_TOP:
            return "chongding";
        // 5. 蹲底
        case ELEVATOR_EXCEPTION_RUN_OVER_BOTTOM:
            return "dundi";
        // 6. 超速
        case ELEVATOR_EXCEPTION_OVERSPEED:
            return "chaosu";
        // 11. 反复开关门
        case ELEVATOR_EXCEPTION_DOOR_REPEATED:
            // ai model define type as door_moving
            return "door_moving";  // "fanfukaiguanmen";
        // 12. 关门异常
        case ELEVATOR_EXCEPTION_DOOR_CLOSE_ERROR:
            // ai model define type as door_open
            return "door_open";  // "guanmenyichang";
        // 202. 电瓶车
        case ELEVATOR_EXCEPTION_EBIKE:
            return "ebike";
        case ELEVATOR_EXCEPTION_NONE:
        default:
            return "";
    }
    return "";
}

int elevator_init(void);

const char* elevator_serialno(void);
const char* elevator_mac(void);
const char* elevator_deviceid(void);
// int elevator_get_status(struct elevator_status* st);

// int elevator_passenger_count(void);
// int elevator_passenger_count_in(void);
// int elevator_passenger_count_out(void);
double elevator_temperature(void);

int elevator_light_brightness();

// it's implement in houqi_liftfault

// not thread safe!
// should only be called in main thread such as uv loop
// maybe you should call statemachine_post_fault in uloop
// immediate: 1 - directly report without pending
//            0 - maybe pending or wait keyevent when manual fault mode

int elevator_fault_occurred(enum elevator_exception fault, int immediate);
int elevator_fault_resolved(enum elevator_exception fault);

int elevator_fault_review(int *type, uint64_t *occurred_ms);

int elevator_fault_is_active(enum elevator_exception fault);
#endif
