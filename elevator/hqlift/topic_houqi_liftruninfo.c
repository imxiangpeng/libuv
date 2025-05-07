// mxp, 20250502, implement houqi topic: /API/V1/Up/LiftRunInfo
// all data is subscribed from elevatord's event
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "cjson/cJSON.h"
#include "elevator.h"
#include "hr_log.h"
#include "uelevator.h"
#include "uviot.h"

#define EVENT_RUNINFO_TOPIC_NAME "LiftRunInfo"
static struct uviot* _iot = NULL;

static int _on_publish(void** payload, int* len) {
    struct tm tm;
    struct timespec ts;
    printf("liftruninfo publish \n");
    char tmp[256] = {0};
    cJSON* root = NULL;

    struct elevator_status st;
    struct elevator_historical* his = NULL;

    memset((void*)&st, 0, sizeof(st));
    // int need_report = 0;

    if (!payload || !len)
        return -1;

    elevator_get_status(&st);

    if (0 != uelevator_get_historical(&his)) {
        return -1;
    }

    root = cJSON_CreateObject();
    if (!root)
        return -1;

    cJSON_AddStringToObject(root, "type", EVENT_RUNINFO_TOPIC_NAME);
    cJSON_AddStringToObject(root, "macAddr", uviot_get_connection_mac_address(_iot));
    cJSON_AddStringToObject(root, "elevatorNo", elevator_deviceid());

    cJSON_AddNumberToObject(root, "runningMileageTotal", his->distance);
    cJSON_AddNumberToObject(root, "mannedNum", st.passenger_count);
    cJSON_AddNumberToObject(root, "inNum", 0 /*dm_lift_passenger_count_in()*/);
    cJSON_AddNumberToObject(root, "outNum", 0 /*dm_lift_passenger_count_out()*/);

    cJSON_AddNumberToObject(root, "runningDirection", his->direction);

    cJSON* arr = cJSON_AddArrayToObject(root, "runSpeed");
    for (size_t i = 0; i < his->speed_array.offset;) {
        double* v = (double*)(his->speed_array.data + i);
        cJSON_AddItemToArray(arr, cJSON_CreateNumber(*v));
        i += sizeof(double);
    }

    arr = cJSON_AddArrayToObject(root, "jitterFrequency");
    for (size_t i = 0; i < his->jitter_frequency_array.offset;) {
        double* v = (double*)(his->jitter_frequency_array.data + i);
        cJSON_AddItemToArray(arr, cJSON_CreateNumber(*v));
        i += sizeof(double);
    }

    arr = cJSON_AddArrayToObject(root, "jitterAcceleration");
    for (size_t i = 0; i < his->jitter_accel_array.offset;) {
        double* v = (double*)(his->jitter_accel_array.data + i);
        cJSON_AddItemToArray(arr, cJSON_CreateNumber(*v));
        i += sizeof(double);
    }

    arr = cJSON_AddArrayToObject(root, "acceleration");
    for (size_t i = 0; i < his->accel_array.offset;) {
        double* v = (double*)(his->accel_array.data + i);
        cJSON_AddItemToArray(arr, cJSON_CreateNumber(*v));
        i += sizeof(double);
    }

    cJSON_AddNumberToObject(root, "runBeginTimeStamp", his->timestamp_begin);
    cJSON_AddNumberToObject(root, "runEndTimeStamp", his->timestamp_end);
    cJSON_AddNumberToObject(root, "temperature", elevator_temperature());
    cJSON_AddNumberToObject(root, "lightVariationAmplitude", 1);

    clock_gettime(CLOCK_REALTIME, &ts);
    (void)localtime_r(&ts.tv_sec, &tm);

    /*size_t size =*/strftime(tmp, sizeof(tmp), "%Y-%m-%d %H:%M:%S", &tm);
    cJSON_AddStringToObject(root, "faultTime", tmp);
    cJSON_AddNumberToObject(root, "runBeginFloor", his->floor_begin);
    cJSON_AddNumberToObject(root, "runEndFloor", his->floor_end);

    *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!*payload)
        return -1;

    *len = strlen(*payload);

    HR_LOGD("publish: %s\n", *payload);
    return 0;
}

static struct uviot_topic _topic_liftruninfo = {
    .name = EVENT_RUNINFO_TOPIC_NAME,
    .topic = "/ API/V1/Up/" EVENT_RUNINFO_TOPIC_NAME,
    .period = 0,
    .type = TOPIC_TYPE_PUBLISH,
    .callback.on_publish = _on_publish,
};

int topic_houqi_liftruninfo_init(struct uviot* iot, const char* public_key, const char* device_name) {
    (void)public_key;
    (void)device_name;
    _iot = iot;
    uviot_topic_register(iot, &_topic_liftruninfo);

    return 0;
}

// trigger publish immediately
void topic_houqi_liftruninfo_post(void) {
    uviot_publish_async(_iot, &_topic_liftruninfo);
}