
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "cjson/cJSON.h"
#include "iot_topic.h"
#include "hr_log.h"
#include "dm_impl.h"
#include "dm_lift.h"

#define EVENT_RUNINFO_TOPIC_NAME "LiftRunInfo"


static char _device_id[256] = {0};

static int _running_direction = LF_DIRECTION_STATIONARY;

static long _run_begin_timestamp = 0;
static long _run_end_timestamp = 0;

static int _on_publish(void** payload, int* len) {
    struct tm tm;
    struct timespec ts;
    printf("liftruninfo publish \n");
    char tmp[256] = {0};
    cJSON* root = NULL;
    int report_direction = _running_direction;
    
    //int need_report = 0;
    
    if (!payload || !len) return -1;

    int direction = dm_lift_direction();

    if (_running_direction == direction) {
        return 0;
    }
 
    // update stored running direction
    _running_direction = direction;

    if (_running_direction == LF_DIRECTION_STATIONARY) {
        _run_end_timestamp = time(NULL);
    } else {
        _run_begin_timestamp = time(NULL);
    }

    // report only when running end
    if (direction != LF_DIRECTION_STATIONARY) {
        return 0;
    }

    root = cJSON_CreateObject();
    if (!root) return -1;

    cJSON_AddStringToObject(root, "type", EVENT_RUNINFO_TOPIC_NAME);
    cJSON_AddStringToObject(root, "macAddr", dm_running_interface_macaddr());
    cJSON_AddStringToObject(root, "elevatorNo", dm_lift_id());
    
    cJSON_AddNumberToObject(root, "runningMileageTotal", dm_lift_travel_distance());
    cJSON_AddNumberToObject(root, "mannedNum", dm_lift_passenger_count());
    cJSON_AddNumberToObject(root, "inNum", dm_lift_passenger_count_in());
    cJSON_AddNumberToObject(root, "outNum", dm_lift_passenger_count_out());

    cJSON_AddNumberToObject(root, "runningDirection", report_direction);
    
    cJSON* arr = cJSON_AddArrayToObject(root, "runSpeed");
    cJSON_AddItemToArray(arr, cJSON_CreateNumber(1.0));
    cJSON_AddItemToArray(arr, cJSON_CreateNumber(2.0));
    cJSON_AddItemToArray(arr, cJSON_CreateNumber(3.0));
    
    arr = cJSON_AddArrayToObject(root, "jitterFrequency");
    cJSON_AddItemToArray(arr, cJSON_CreateNumber(1.0));
    cJSON_AddItemToArray(arr, cJSON_CreateNumber(2.0));
    cJSON_AddItemToArray(arr, cJSON_CreateNumber(3.0));   

    arr = cJSON_AddArrayToObject(root, "jitterAcceleration");
    cJSON_AddItemToArray(arr, cJSON_CreateNumber(1.0));
    cJSON_AddItemToArray(arr, cJSON_CreateNumber(2.0));
    cJSON_AddItemToArray(arr, cJSON_CreateNumber(3.0));  

    arr = cJSON_AddArrayToObject(root, "acceleration");
    cJSON_AddItemToArray(arr, cJSON_CreateNumber(1.0));
    cJSON_AddItemToArray(arr, cJSON_CreateNumber(2.0));
    cJSON_AddItemToArray(arr, cJSON_CreateNumber(3.0));  


    cJSON_AddNumberToObject(root, "runBeginTimeStamp", _run_begin_timestamp);
    cJSON_AddNumberToObject(root, "runEndTimeStamp", _run_end_timestamp);
    cJSON_AddNumberToObject(root, "temperature", dm_lift_temperature());
    cJSON_AddNumberToObject(root, "lightVariationAmplitude", 1);
    
    clock_gettime(CLOCK_REALTIME, &ts);
    (void)localtime_r(&ts.tv_sec, &tm);

    /*size_t size =*/ strftime(tmp, sizeof(tmp), "%Y-%m-%d %H:%M:%S", &tm);
    cJSON_AddStringToObject(root, "faultTime", tmp);
    cJSON_AddNumberToObject(root, "runBeginFloor", dm_lift_floor());
    cJSON_AddNumberToObject(root, "runEndFloor", dm_lift_floor());

    *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!*payload) return -1;

    *len = strlen(*payload);
    
    
    HR_LOGD("publish: %s\n", *payload);
    return 0;
}

static struct iot_topic dm_topic_liftruninfo = {
    .name = EVENT_RUNINFO_TOPIC_NAME,
    .topic = "/API/V1/Up/" EVENT_RUNINFO_TOPIC_NAME,
     // detect every seconds, but report only when running finished
    .period = 1000,
    .type = TOPIC_TYPE_PUBLISH,
    .callback.on_publish = _on_publish,
};

int hq_topic_liftruninfo_init(const char* public_key, const char* device_name) {
  (void)public_key;
  (void)device_name;
    dm_impl_system_property_get(PROPERTY_DEVICEID, _device_id, sizeof(_device_id));
    _running_direction = dm_lift_direction();
    iot_topic_register(&dm_topic_liftruninfo);

    return 0;
}
