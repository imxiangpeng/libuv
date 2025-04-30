
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "elevator.h"
#include "iot_topic.h"
/// publish every 10s
#include "cjson/cJSON.h"
#include "elevator.h"
#include "hr_log.h"
#include "platform.h"

#define EVENT_LIFTSTATE_TOPIC_NAME "LiftState"

static int _running_direction = ELEVATOR_DIR_STATIONARY;
// report when begin and finish
// stationary -> up/down
// up/down -> stationary
static int _on_publish(void** payload, int* len) {
    struct tm tm;
    struct timespec ts;
    printf("liftstate publish \n");
    char tmp[256] = {0};

    int report_direction = _running_direction;

    int direction = elevator_direction();
    if (_running_direction == direction) {
        return 0;
    }

    // only report running direction not stationary
    if (ELEVATOR_DIR_STATIONARY == report_direction) {
        report_direction = direction;
    }

    cJSON* root = cJSON_CreateObject();
    if (!root)
        return -1;

    cJSON_AddStringToObject(root, "type", EVENT_LIFTSTATE_TOPIC_NAME);
    cJSON_AddStringToObject(root, "macAddr", platform_get_const_mac_address());
    cJSON_AddStringToObject(root, "elevatorNo", elevator_deviceid());
    cJSON_AddNumberToObject(root, "faultType", 0);

    clock_gettime(CLOCK_REALTIME, &ts);
    (void)localtime_r(&ts.tv_sec, &tm);

    /*size_t size =*/strftime(tmp, sizeof(tmp), "%Y-%m-%d %H:%M:%S", &tm);

    cJSON_AddStringToObject(root, "faultTime", tmp);
    cJSON_AddNumberToObject(root, "currentFloor", elevator_floor());
    cJSON_AddNumberToObject(root, "currentSpeed", elevator_speed());
    cJSON_AddNumberToObject(root, "runningDirection", report_direction);
    cJSON_AddNumberToObject(root, "doorStatus", elevator_door());
    cJSON_AddNumberToObject(root, "personInLift", elevator_passenger_count());

    *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!*payload)
        return -1;

    *len = strlen(*payload);
    HR_LOGD("publish: %s\n", *payload);
#if 0    
    *payload = strdup("{\"name\":\"xiaohua\"}");
    *len = strlen(*payload);
#endif
    return 0;
}

static struct iot_topic _topic_liftstate = {
    .name = EVENT_LIFTSTATE_TOPIC_NAME,
    .topic = "/API/V1/Up/" EVENT_LIFTSTATE_TOPIC_NAME,
    .period = 1000,
    .type = TOPIC_TYPE_PUBLISH,
    .callback.on_publish = _on_publish,
};

int topic_houqi_liftstate_init(const char* public_key, const char* device_name) {
    (void)public_key;
    (void)device_name;
    _running_direction = elevator_direction();
    iot_topic_register(&_topic_liftstate);

    return 0;
}

// trigger publish immediately
void topic_houqi_liftstate_post(void) {
    iot_topic_public_async(&_topic_liftstate);
}