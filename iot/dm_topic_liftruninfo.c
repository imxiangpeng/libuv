
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "cjson/cJSON.h"
#include "dm_topic.h"
#include "hr_log.h"

#define EVENT_RUNINFO_TOPIC_NAME "LiftRunInfo"

static const char* _elevator_no(void) {
    return "1234567890";
}

static const char* _mac_address(void) {
    return "FA1FADCDD2SQ";
}

static int _current_floor() {
    return 10;
}

static double _current_speed() {
    return 2.5;
}
static int _running_direction() {
    return 1;  // 1: up, 2: down
}

static int _door_status() {
    return 1;  // 0: open, 1: close
}
static int _person_nums() {
    return 6;  // 0: open, 1: close
}
static int _on_publish(void** payload, int* len) {
    struct tm tm;
    struct timespec ts;
    printf("liftruninfo publish \n");
    char tmp[256] = {0};
    cJSON* root = cJSON_CreateObject();
    if (!root) return -1;

    cJSON_AddStringToObject(root, "type", EVENT_RUNINFO_TOPIC_NAME);
    cJSON_AddStringToObject(root, "macAddr", "02424feea717");
    cJSON_AddStringToObject(root, "elevatorNo", _elevator_no());
    cJSON_AddNumberToObject(root, "faultType", 0);

    clock_gettime(CLOCK_REALTIME, &ts);
    (void)localtime_r(&ts.tv_sec, &tm);

    size_t size = strftime(tmp, sizeof(tmp), "%Y-%m-%d %H:%M:%S", &tm);
    cJSON_AddStringToObject(root, "faultTime", tmp);
    cJSON_AddNumberToObject(root, "currentFloor", _current_floor());
    cJSON_AddNumberToObject(root, "currentSpeed", _current_speed());
    cJSON_AddNumberToObject(root, "runningDirection", _running_direction());
    cJSON_AddNumberToObject(root, "doorStatus", _door_status());
    cJSON_AddNumberToObject(root, "personInLift", _person_nums());

    *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!*payload) return -1;

    *len = strlen(*payload);
    HR_LOGD("publish: %s\n", *payload);
    return 0;
}

static struct dm_topic dm_topic_liftruninfo = {
    .name = EVENT_RUNINFO_TOPIC_NAME,
    .topic = "/API/V1/Up/" EVENT_RUNINFO_TOPIC_NAME,
    .period = 0,
    .type = TOPIC_TYPE_PUBLISH,
    .callback.on_publish = _on_publish,
};

int dm_topic_liftruninfo_init(void) {
    dm_topic_register(&dm_topic_liftruninfo);
    return 0;
}
