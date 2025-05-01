
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
#include "uelevator.h"
#include "uviot.h"

#define EVENT_LIFTSTATE_TOPIC_NAME "LiftState"

static enum elevator_direction _running_direction = ELEVATOR_DIR_STATIONARY;
// report when begin and finish
// stationary -> up/down
// up/down -> stationary
static int _on_publish(void** payload, int* len) {
    struct tm tm;
    struct timespec ts;
    printf("liftstate publish \n");
    // char tmp[256] = {0};

    struct elevator_status st;

    uelevator_get_status(&st);

    // only update direction when running
    // it's no stationary on houqi platform
    if (st.direction != ELEVATOR_DIR_STATIONARY) {
        _running_direction = st.direction;
    }

    cJSON* root = cJSON_CreateObject();
    if (!root)
        return -1;

    cJSON_AddStringToObject(root, "type", EVENT_LIFTSTATE_TOPIC_NAME);
    cJSON_AddStringToObject(root, "macAddr", platform_get_connection_mac_address());
    cJSON_AddStringToObject(root, "elevatorNo", elevator_deviceid());
    cJSON_AddNumberToObject(root, "faultType", 0);

    clock_gettime(CLOCK_REALTIME, &ts);
    (void)localtime_r(&ts.tv_sec, &tm);
#if 0
    /*size_t size =*/strftime(tmp, sizeof(tmp), "%Y-%m-%d %H:%M:%S", &tm);
    // it's not mandatory
    cJSON_AddStringToObject(root, "faultTime", tmp);
#endif
    cJSON_AddNumberToObject(root, "currentFloor", st.current_floor);
    cJSON_AddNumberToObject(root, "currentSpeed", st.speed);
    cJSON_AddNumberToObject(root, "runningDirection", _running_direction);
    cJSON_AddNumberToObject(root, "doorStatus", st.current_floor);
    cJSON_AddNumberToObject(root, "personInLift", elevator_passenger_count());

    *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!*payload) {
        return -1;
    }
    *len = strlen(*payload);
    HR_LOGD("publish: %s\n", *payload);
#if 0
    *payload = strdup("{\"name\":\"xiaohua\"}");
    *len = strlen(*payload);
#endif
    return 0;
}

static struct uviot_topic _topic_liftstate = {
    .name = EVENT_LIFTSTATE_TOPIC_NAME,
    .topic = "/API/V1/Up/" EVENT_LIFTSTATE_TOPIC_NAME,
    .period = 1000,  // 大华好像配置的是 500ms
    .type = TOPIC_TYPE_PUBLISH,
    .callback.on_publish = _on_publish,
};

static struct uviot* _iot = NULL;
int topic_houqi_liftstate_init(struct uviot* iot, const char* public_key, const char* device_name) {
    (void)public_key;
    (void)device_name;
    _running_direction = elevator_direction();
    _iot = iot;
    uviot_topic_register(iot, &_topic_liftstate);


    return 0;
}

// trigger publish immediately
void topic_houqi_liftstate_post(void) {
    uviot_publish_async(_iot, &_topic_liftstate);
}