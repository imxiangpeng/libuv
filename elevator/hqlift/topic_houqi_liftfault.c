// mxp, 20250505, implement houqi topic: /API/V1/Up/LiftFault
// need implement ...

#include <stdio.h>
#include <string.h>
#include <time.h>

#include "elevator.h"
#include "uviot.h"
#include "cjson/cJSON.h"
#include "hr_log.h"
#include "platform.h"
#include "uelevator.h"

#define EVENT_FAULT_TOPIC_NAME "LiftFault"
static struct uviot* _iot = NULL;

static int _on_publish(void **payload, int *len) {
    struct tm tm;
    struct timespec ts;
    printf("liftfault publish \n");
    char tmp[256] = {0};
    struct elevator_status st;
    cJSON *root = cJSON_CreateObject();
    if (!root) return -1;

    uelevator_get_status(&st);
    cJSON_AddStringToObject(root, "type", "LiftFault");
    cJSON_AddStringToObject(root, "macAddr", uviot_get_connection_mac_address(_iot));
    cJSON_AddStringToObject(root, "uuid", "00000000000000000");
    cJSON_AddStringToObject(root, "elevatorNo", elevator_deviceid());
    cJSON_AddNumberToObject(root, "currentSpeed", st.speed);
    cJSON_AddNumberToObject(root, "runningDirection", st.direction);
    cJSON_AddNumberToObject(root, "doorStatus", st.door_state);
    cJSON_AddNumberToObject(root, "personInLift", elevator_passenger_count());
    cJSON_AddNumberToObject(root, "currentFloor", st.current_floor);

    cJSON *arr = cJSON_AddArrayToObject(root, "ErrorListBean");
    cJSON *fault = cJSON_CreateObject();
    cJSON_AddItemToArray(arr, fault);
    cJSON_AddStringToObject(fault, "keyId", "00000000000000");
    cJSON_AddNumberToObject(fault, "faultType", 0);

    clock_gettime(CLOCK_REALTIME, &ts);
    (void)localtime_r(&ts.tv_sec, &tm);

    /*size_t size =*/ strftime(tmp, sizeof(tmp), "%Y-%m-%d %H:%M:%S", &tm);
    cJSON_AddStringToObject(fault, "faultBeginTime", tmp);
    cJSON_AddStringToObject(fault, "faultEndTime", tmp);
    cJSON_AddStringToObject(fault, "faultVideoUrl", "ftp://....");

    *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!*payload) return -1;

    *len = strlen(*payload);
    HR_LOGD("publish: %s\n", *payload);
    return 0;
}

static struct uviot_topic dm_topic_liftfault = {
    .name = EVENT_FAULT_TOPIC_NAME,
    .topic = "/API/V1/Up/" EVENT_FAULT_TOPIC_NAME,
    .period = 0,
    .type = TOPIC_TYPE_PUBLISH,
    .callback.on_publish = _on_publish,
};

int topic_houqi_liftfault_init(struct uviot* iot,const char* public_key, const char* device_name) {
  (void)public_key;
  (void)device_name;
    _iot = iot;
    uviot_topic_register(iot, &dm_topic_liftfault);
    return 0;
}
