
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "dm_impl.h"

#include "iot_topic.h"
/// publish every 10s
#include "cjson/cJSON.h"
#include "hr_log.h"
#include "dm_impl.h"
#include "dm_lift.h"

#define EVENT_LIFTSTATE_TOPIC_NAME "LiftState"

static int _running_direction = LF_DIRECTION_STATIONARY;
static int _door_status() {
    return 1;  // 0: open, 1: close
}
static int _person_nums() {
    return 6;  // 0: open, 1: close
}

// report when begin and finish
// stationary -> up/down
// up/down -> stationary
static int _on_publish(void** payload, int* len) {
    struct tm tm;
    struct timespec ts;
    printf("liftstate publish \n");
    char tmp[256] = {0};
    
    int report_direction = _running_direction;

    int direction = dm_lift_direction();
    if (_running_direction == direction) {
        return 0;
    }

    // only report running direction not stationary
    if (LF_DIRECTION_STATIONARY == report_direction) {
        report_direction = direction;
    }


    cJSON* root = cJSON_CreateObject();
    if (!root) return -1;

    cJSON_AddStringToObject(root, "type", EVENT_LIFTSTATE_TOPIC_NAME);
    cJSON_AddStringToObject(root, "macAddr", dm_running_interface_macaddr());
    cJSON_AddStringToObject(root, "elevatorNo", dm_lift_id());
    cJSON_AddNumberToObject(root, "faultType", 0);

    clock_gettime(CLOCK_REALTIME, &ts);
    (void)localtime_r(&ts.tv_sec, &tm);

    /*size_t size =*/ strftime(tmp, sizeof(tmp), "%Y-%m-%d %H:%M:%S", &tm);

    cJSON_AddStringToObject(root, "faultTime", tmp);
    cJSON_AddNumberToObject(root, "currentFloor", dm_lift_floor());
    cJSON_AddNumberToObject(root, "currentSpeed", dm_lift_speed());
    cJSON_AddNumberToObject(root, "runningDirection", report_direction);
    cJSON_AddNumberToObject(root, "doorStatus", dm_lift_door());
    cJSON_AddNumberToObject(root, "personInLift", dm_lift_passenger_count());

    *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!*payload) return -1;

    *len = strlen(*payload);
    HR_LOGD("publish: %s\n", *payload);
#if 0    
    *payload = strdup("{\"name\":\"xiaohua\"}");
    *len = strlen(*payload);
#endif
    return 0;
}

static struct iot_topic dm_topic_liftstate = {
    .name = EVENT_LIFTSTATE_TOPIC_NAME,
    .topic = "/API/V1/Up/" EVENT_LIFTSTATE_TOPIC_NAME,
    .period = 1000,
    .type = TOPIC_TYPE_PUBLISH,
    .callback.on_publish = _on_publish,
};

int hq_topic_liftstate_init(const char* public_key, const char* device_name) {

  (void)public_key;
  (void)device_name;
    _running_direction = dm_lift_direction();
    iot_topic_register(&dm_topic_liftstate);

    return 0;
}
