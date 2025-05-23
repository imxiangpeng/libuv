// mxp, 20250522, implement houqi topic: /API/V1/Up/LiftFault

#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <uuid/uuid.h>

#include "cjson/cJSON.h"
#include "elevator.h"
#include "hr_list.h"
#include "hr_log.h"
#include "time_utils.h"
#include "uelevator.h"
#include "uviot.h"

#define EVENT_FAULT_TOPIC_NAME "LiftFault"

// fault event only in memory do not save
struct lift_fault_event {
    enum elevator_exception type;
    int64_t fault_begin_time;  // please convert when you report
    int64_t fault_end_time;
    char* video_url;

    struct hr_list_head entry;
};

static struct uviot* _iot = NULL;
// queue ready to publish
static HR_LIST_HEAD(_lift_fault_message_queue);
// pending fault which have publish but not finished
static HR_LIST_HEAD(_lift_fault_idle_queue);

static pthread_mutex_t _queue_lock;

static int to_houqi_fault(enum elevator_exception fault) {
    switch (fault) {
            // 1. 困人
        case ELEVATOR_EXCEPTION_PEOPLE_TRAPPED:
            return 1;
        // 2. 开门走车
        case ELEVATOR_EXCEPTION_RUN_WITHOUT_DOOR_CLOSED:
            return 2;
        // 3. 非门区停车
        case ELEVATOR_EXCEPTION_STOPPED_NOT_AT_DOOR:
            return 3;
        // 4. 冲顶
        case ELEVATOR_EXCEPTION_RUN_OVER_TOP:
            return 4;
        // 5. 蹲底
        case ELEVATOR_EXCEPTION_RUN_OVER_BOTTOM:
            return 5;
        // 6. 超速
        case ELEVATOR_EXCEPTION_OVERSPEED:
            return 6;
        case ELEVATOR_EXCEPTION_NONE:
        default:
            return 0;
    }
}
static int _on_publish(void** payload, int* len) {
    struct tm tm;
    struct timespec ts;

    struct lift_fault_event* e = NULL;
    struct elevator_status st;

    uuid_t uuid;
    char uuid_str[UUID_STR_LEN];

    char tmp[256] = {0};

    cJSON* root = cJSON_CreateObject();
    if (!root)
        return -1;

    pthread_mutex_lock(&_queue_lock);
    if (hr_list_empty(&_lift_fault_message_queue)) {
        pthread_mutex_unlock(&_queue_lock);
        return -1;
    }

    e = hr_list_first_entry(&_lift_fault_message_queue, struct lift_fault_event, entry);
    // take off from list
    hr_list_del(&e->entry);
    pthread_mutex_unlock(&_queue_lock);

    uuid_generate(uuid);

    //uuid_unparse(uuid, uuid_str);
    // only use 20 char
    for (size_t i = 0; i <sizeof(uuid)/sizeof(uuid[0]); i++) {
        snprintf(&uuid_str[i * 2], 3,"%02x", uuid[i]);
    }
    uuid_str[20] = '\0';

    printf("uuid:%s\n", uuid_str);

    uelevator_get_status(&st);
    cJSON_AddStringToObject(root, "type", "LiftFault");
    // cJSON_AddStringToObject(root, "macAddr", uviot_get_connection_mac_address(_iot));
    // houqi's macAddr is serialno, length must > 12
    cJSON_AddStringToObject(root, "macAddr", elevator_serialno());  // elevator_mac
    // dahua use 20 chars, such as: "uuid":"f840fe850000ebd46e3c"                                                                    
    cJSON_AddStringToObject(root, "uuid", uuid_str);
    cJSON_AddStringToObject(root, "elevatorNo", elevator_deviceid());
    cJSON_AddNumberToObject(root, "currentSpeed", st.speed);
    cJSON_AddNumberToObject(root, "runningDirection", st.direction);
    cJSON_AddNumberToObject(root, "doorStatus", st.door_state);
    cJSON_AddNumberToObject(root, "personInLift", st.passenger_count);
    cJSON_AddNumberToObject(root, "currentFloor", st.current_floor);

    // temperature & lightVariationAmplitude are in document, but dahua report it
    cJSON_AddNumberToObject(root, "temperature", elevator_temperature());
    cJSON_AddNumberToObject(root, "lightVariationAmplitude", elevator_light_brightness());
    
    
    cJSON* arr = cJSON_AddArrayToObject(root, "ErrorListBean");
    cJSON* fault = cJSON_CreateObject();
    cJSON_AddItemToArray(arr, fault);
    cJSON_AddStringToObject(fault, "keyId", uuid_str);
    cJSON_AddNumberToObject(fault, "faultType", to_houqi_fault(e->type));

    ts.tv_sec = e->fault_begin_time / 1000;
    (void)localtime_r(&ts.tv_sec, &tm);
    /*size_t size =*/strftime(tmp, sizeof(tmp), "%Y-%m-%d %H:%M:%S", &tm);
    cJSON_AddStringToObject(fault, "faultBeginTime", tmp);
    memset((void*)tmp, 0, sizeof(tmp));
    if (e->fault_end_time != 0) {
        ts.tv_sec = e->fault_end_time / 1000;
        (void)localtime_r(&ts.tv_sec, &tm);
        /*size_t size =*/strftime(tmp, sizeof(tmp), "%Y-%m-%d %H:%M:%S", &tm);
    }
    cJSON_AddStringToObject(fault, "faultEndTime", tmp);
    cJSON_AddStringToObject(fault, "faultVideoUrl", "");

    // the fault have finished, release it
    if (e->fault_end_time == 0) {
        // we should keep it for finish event
        pthread_mutex_lock(&_queue_lock);
        hr_list_add_tail(&e->entry, &_lift_fault_idle_queue);
        pthread_mutex_unlock(&_queue_lock);
    } else {
        // now we can free event
        HR_INIT_LIST_HEAD(&e->entry);
        free(e);
    }
    *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!*payload)
        return -1;

    *len = strlen(*payload);
    HR_LOGD("publish: %s\n", (char*)*payload);
    return 0;
}

static struct uviot_topic dm_topic_liftfault = {
    .name = EVENT_FAULT_TOPIC_NAME,
    .topic = "/API/V1/Up/" EVENT_FAULT_TOPIC_NAME,
    .period = 0,
    .type = TOPIC_TYPE_PUBLISH,
    .callback.on_publish = _on_publish,
};

int topic_houqi_liftfault_init(struct uviot* iot, const char* public_key, const char* device_name) {
    (void)public_key;
    (void)device_name;
    _iot = iot;

    pthread_mutex_init(&_queue_lock, NULL);

    uviot_topic_register(iot, &dm_topic_liftfault);
    return 0;
}

static struct lift_fault_event* fault_event_alloc() {
    struct lift_fault_event* e = (struct lift_fault_event*)calloc(1, sizeof(struct lift_fault_event));
    if (!e) {
        return NULL;
    }

    HR_INIT_LIST_HEAD(&e->entry);

    return e;
}

static void fault_event_free(struct lift_fault_event* e) {
    if (!e) {
        return;
    }

    pthread_mutex_lock(&_queue_lock);
    hr_list_del(&e->entry);
    pthread_mutex_unlock(&_queue_lock);

    HR_INIT_LIST_HEAD(&e->entry);

    free(e);
}

static int publish_fault_event(struct lift_fault_event* e) {
    pthread_mutex_lock(&_queue_lock);
    hr_list_add_tail(&e->entry, &_lift_fault_message_queue);
    pthread_mutex_unlock(&_queue_lock);
    return uviot_publish_async(_iot, &dm_topic_liftfault);
}

int elevator_fault_occurred(enum elevator_exception fault) {
    struct lift_fault_event* e = fault_event_alloc();
    if (!e) {
        return -1;
    }

    HR_LOGD("%s(%d): fault:0x%X\n", __FUNCTION__, __LINE__, fault);
    e->type = fault;

    e->fault_begin_time = get_realtime_ms();
    publish_fault_event(e);
    return 0;
}

// the same fault can not report more than once, before it end
int elevator_fault_resolved(enum elevator_exception fault) {
    struct lift_fault_event* e = NULL;
    // we should lookup in idle list
    // ignore when can not find
    HR_LOGD("%s(%d): fault:0x%X\n", __FUNCTION__, __LINE__, fault);
    pthread_mutex_lock(&_queue_lock);
    hr_list_for_each_entry(e, &_lift_fault_idle_queue, entry) {
        if (e->type == fault) {
            break;
        }
    }

    if (!e) {
        return -1;
    }
    // take off from idle queue
    hr_list_del(&e->entry);

    e->fault_end_time = get_realtime_ms();
    pthread_mutex_unlock(&_queue_lock);

    publish_fault_event(e);
    return 0;
}

int elevator_fault_review(int *type, uint64_t *occurred_ms) {
    struct lift_fault_event *e = NULL;

    if (!type || !occurred_ms) {
        return -1;
    }

    pthread_mutex_lock(&_queue_lock);
    if (hr_list_empty(&_lift_fault_idle_queue)) {
        pthread_mutex_unlock(&_queue_lock);
        return 0;
    }

    e = hr_list_first_entry(&_lift_fault_idle_queue, struct lift_fault_event, entry);
    *type = to_houqi_fault(e->type);
    *occurred_ms = e->fault_begin_time;

    pthread_mutex_unlock(&_queue_lock);


    return 0;
}