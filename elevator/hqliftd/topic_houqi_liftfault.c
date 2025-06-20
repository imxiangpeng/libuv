// mxp, 20250522, implement houqi topic: /API/V1/Up/LiftFault

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <uuid/uuid.h>

#include "cjson/cJSON.h"
#include "elevator.h"
#include "hr_list.h"
#include "hr_log.h"
#include "sconf.h"
#include "time_utils.h"
#include "uelevator.h"
#include "uviot.h"

#define EVENT_FAULT_TOPIC_NAME "LiftFault"
#define LIFTFAULT_REPORT_LIMIT_PER_DAY 3
#define LIFTFAULT_REPORT_EVENT_VIDEO_DURATION 10
#define LIFTFAULT_REPORT_FAULT_VIDEO_MARGIN_SECONDS 120

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

enum {

    OPTION_FIELD_FTP_ADDRESS = 0,
    OPTION_FIELD_FTP_USERNAME,
    OPTION_FIELD_FTP_PASSWORD,
    OPTION_FAULT_REPORT_SWITCH,
    OPTION_FAULT_REPORT_LIMIT_PER_DAY,

};
static struct sconf_proto _options[] = {
    [OPTION_FIELD_FTP_ADDRESS] = {"FTP_ADDRESS", PROTO_VALUE_STRING, {.string = "ftp://ftp.hqszjs.com:2100"}},
    [OPTION_FIELD_FTP_USERNAME] = {"FTP_USERNAME", PROTO_VALUE_STRING, {.string = "inspur"}},
    [OPTION_FIELD_FTP_PASSWORD] = {"FTP_PASSWORD", PROTO_VALUE_STRING, {.string = "inspur88*"}},
    [OPTION_FAULT_REPORT_SWITCH] = {"LIFTFAULT_REPORT_SWITCH", PROTO_VALUE_INT64, {.int64 = 1}},
    [OPTION_FAULT_REPORT_LIMIT_PER_DAY] = {"LIFTFAULT_REPORT_LIMIT_PER_DAY", PROTO_VALUE_INT64, {.int64 = LIFTFAULT_REPORT_LIMIT_PER_DAY}},  // default 3
};

// no persist storage
static struct fault_report_statistics {
    enum elevator_exception exception;
    int report_count;
} _fault_report_statistics[] = {
    {.exception = ELEVATOR_EXCEPTION_PEOPLE_TRAPPED, 0},
    {.exception = ELEVATOR_EXCEPTION_RUN_WITHOUT_DOOR_CLOSED, 0},
    {.exception = ELEVATOR_EXCEPTION_STOPPED_NOT_AT_DOOR, 0},
    {.exception = ELEVATOR_EXCEPTION_RUN_OVER_TOP, 0},
    {.exception = ELEVATOR_EXCEPTION_RUN_OVER_BOTTOM, 0},
    {.exception = ELEVATOR_EXCEPTION_OVERSPEED, 0},
};

// date to record and reset report statistics
static struct tm _fault_report_statistics_tm;

static void upload_fault_video(struct lift_fault_event* e);

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
        cJSON_Delete(root);
        return -1;
    }

    e = hr_list_first_entry(&_lift_fault_message_queue, struct lift_fault_event, entry);
    // take off from list
    hr_list_del(&e->entry);
    pthread_mutex_unlock(&_queue_lock);

    uuid_generate(uuid);

    uuid_unparse(uuid, uuid_str);
    // only use 20 char
    // for (size_t i = 0; i <sizeof(uuid)/sizeof(uuid[0]); i++) {
    //     snprintf(&uuid_str[i * 2], 3,"%02x", uuid[i]);
    // }
    // uuid_str[20] = '\0';

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

    if (_options[OPTION_FAULT_REPORT_SWITCH].value.int64 == 0) {
        cJSON_Delete(root);
        return 0;
    }

    *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!*payload)
        return -1;

    *len = strlen(*payload);
    HR_LOGD("publish: %s\n", (char*)*payload);
    *len = 0;
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

    sconf_load_with_proto(HQLIFTD_CONFIG_PATH, _options, sizeof(_options) / sizeof(_options[0]));

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
    struct lift_fault_event* e = NULL;
    struct fault_report_statistics* s = NULL;

    struct tm tm;
    struct timespec ts;

    clock_gettime(CLOCK_REALTIME, &ts);
    (void)localtime_r(&ts.tv_sec, &tm);

    if (_fault_report_statistics_tm.tm_year != tm.tm_year ||
        _fault_report_statistics_tm.tm_mon != tm.tm_mon ||
        _fault_report_statistics_tm.tm_mday != tm.tm_mday) {
        HR_LOGD("%s(%d): reset limit %d-%d-%d last %d-%d-%d...\n", __FUNCTION__, __LINE__,
                tm.tm_year + 1900, tm.tm_mon, tm.tm_mday,
                _fault_report_statistics_tm.tm_year + 1900, _fault_report_statistics_tm.tm_mon, _fault_report_statistics_tm.tm_mday);

        memcpy((void*)&_fault_report_statistics_tm, (void*)&tm, sizeof(struct tm));
        // reset limit
        for (size_t i = 0; i < sizeof(_fault_report_statistics) / sizeof(_fault_report_statistics[0]); i++) {
            _fault_report_statistics[i].report_count = 0;
        }
    }

    for (size_t i = 0; i < sizeof(_fault_report_statistics) / sizeof(_fault_report_statistics[0]); i++) {
        if (fault == _fault_report_statistics[i].exception) {
            s = &_fault_report_statistics[i];
            break;
        }
    }

    if (!s) {
        HR_LOGD("%s(%d): not support fault:0x%X\n", __FUNCTION__, __LINE__, fault);
        return -1;
    }

    if (s->report_count >= _options[OPTION_FAULT_REPORT_LIMIT_PER_DAY].value.int64) {
        HR_LOGD("%s(%d): fault:0x%X, reach report limit count:%d\n", __FUNCTION__, __LINE__, fault, s->report_count);
        return -1;
    }

    pthread_mutex_lock(&_queue_lock);

    if (!hr_list_empty(&_lift_fault_idle_queue)) {
        struct lift_fault_event* f = NULL;
        hr_list_for_each_entry(f, &_lift_fault_idle_queue, entry) {
            if (f->type == fault) {
                HR_LOGE("%s(%d): fault:0x%X is occurring, do not report again\n", __FUNCTION__, __LINE__, fault);

                pthread_mutex_unlock(&_queue_lock);
                return -1;
            }
        }
    }

    pthread_mutex_unlock(&_queue_lock);

    e = fault_event_alloc();
    if (!e) {
        return -1;
    }

    // no save to persist storage
    s->report_count++;
    HR_LOGD("%s(%d): fault:0x%X, count:%d\n", __FUNCTION__, __LINE__, fault, s->report_count);
    e->type = fault;

    e->fault_begin_time = get_realtime_ms();

    if (e->type != ELEVATOR_EXCEPTION_PEOPLE_TRAPPED) {
        // e->fault_begin_time = 1749711827000 ;/// 1749740645000;
        upload_fault_video(e);
    }

    publish_fault_event(e);
    return 0;
}

// the same fault can not report more than once, before it end
int elevator_fault_resolved(enum elevator_exception fault) {
    struct lift_fault_event *e = NULL, *f = NULL;
    // we should lookup in idle list
    // ignore when can not find
    HR_LOGD("%s(%d): fault:0x%X\n", __FUNCTION__, __LINE__, fault);
    pthread_mutex_lock(&_queue_lock);

    if (hr_list_empty(&_lift_fault_idle_queue)) {
        pthread_mutex_unlock(&_queue_lock);
        return -1;
    }

    hr_list_for_each_entry(f, &_lift_fault_idle_queue, entry) {
        if (f->type == fault) {
            e = f;
            break;
        }
    }

    if (!e) {
        pthread_mutex_unlock(&_queue_lock);
        return -1;
    }

    // take off from idle queue
    hr_list_del(&e->entry);

    e->fault_end_time = get_realtime_ms();
    pthread_mutex_unlock(&_queue_lock);

    if (e->type == ELEVATOR_EXCEPTION_PEOPLE_TRAPPED) {
        upload_fault_video(e);
    }

    publish_fault_event(e);
    return 0;
}

int elevator_fault_review(int* type, uint64_t* occurred_ms) {
    struct lift_fault_event* e = NULL;

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

// should convert utc timestamp to local timestamp
static void upload_fault_video(struct lift_fault_event* e) {
    struct tm tm;
    struct timespec ts;

    char begin_str[64] = {0};
    char end_str[64] = {0};

    char url[LINE_MAX] = {0};

    char name[64] = {0};

    if (!e) {
        return;
    }

    memset((void*)&tm, 0, sizeof(tm));
    memset((void*)&ts, 0, sizeof(ts));

    switch (e->type) {
        case ELEVATOR_EXCEPTION_RUN_WITHOUT_DOOR_CLOSED:
        case ELEVATOR_EXCEPTION_STOPPED_NOT_AT_DOOR:
        case ELEVATOR_EXCEPTION_RUN_OVER_TOP:
        case ELEVATOR_EXCEPTION_RUN_OVER_BOTTOM:
        case ELEVATOR_EXCEPTION_OVERSPEED:
            // report video 10 seconds around event

            ts.tv_sec = e->fault_begin_time / 1000;
            (void)localtime_r(&ts.tv_sec, &tm);
            /*size_t size =*/strftime(name, sizeof(name), "%Y%m%d_%H%M%S.mp4", &tm);

            memset((void*)&tm, 0, sizeof(tm));
            memset((void*)&ts, 0, sizeof(ts));
            ts.tv_sec = e->fault_begin_time / 1000 - LIFTFAULT_REPORT_EVENT_VIDEO_DURATION / 2;
            (void)localtime_r(&ts.tv_sec, &tm);
            /*size_t size =*/strftime(begin_str, sizeof(begin_str), "%Y%m%d%H%M%S", &tm);


            memset((void*)&tm, 0, sizeof(tm));
            memset((void*)&ts, 0, sizeof(ts));
            ts.tv_sec = e->fault_begin_time / 1000 + LIFTFAULT_REPORT_EVENT_VIDEO_DURATION / 2;
            (void)localtime_r(&ts.tv_sec, &tm);
            /*size_t size =*/strftime(end_str, sizeof(end_str), "%Y%m%d%H%M%S", &tm);

            snprintf(url, sizeof(url), "ftp://ftp.hqszjs.com:2100/event_files/%s/%s", elevator_deviceid(), name);
            break;

        case ELEVATOR_EXCEPTION_PEOPLE_TRAPPED:
            // report when finished
            if (e->fault_end_time == 0) {
                return;
            }
            ts.tv_sec = e->fault_begin_time / 1000;
            (void)localtime_r(&ts.tv_sec, &tm);
            /*size_t size =*/strftime(name, sizeof(name), "%Y%m%d_%H%M%S.mp4", &tm);

            memset((void*)&tm, 0, sizeof(tm));
            memset((void*)&ts, 0, sizeof(ts));

            ts.tv_sec = e->fault_begin_time / 1000 - LIFTFAULT_REPORT_FAULT_VIDEO_MARGIN_SECONDS;
            (void)localtime_r(&ts.tv_sec, &tm);
            /*size_t size =*/strftime(name, sizeof(name), "%Y%m%d_%H%M%S.mp4", &tm);
            /*size_t size =*/strftime(begin_str, sizeof(begin_str), "%Y%m%d%H%M%S", &tm);

            memset((void*)&tm, 0, sizeof(tm));
            memset((void*)&ts, 0, sizeof(ts));
            ts.tv_sec = e->fault_end_time / 1000 + LIFTFAULT_REPORT_FAULT_VIDEO_MARGIN_SECONDS;
            (void)localtime_r(&ts.tv_sec, &tm);
            /*size_t size =*/strftime(end_str, sizeof(end_str), "%Y%m%d%H%M%S", &tm);

            snprintf(url, sizeof(url), "ftp://ftp.hqszjs.com:2100/event_files/%s/%s", elevator_deviceid(), name);
            break;

        default:
            // ignore
            return;
    }

    if (strlen(begin_str) == 0 || strlen(end_str) == 0 || strlen(url) == 0) {
        return;
    }

    // do fork ...

    pid_t pid = fork();

    if (pid < 0) {
        HR_LOGE("can not start estreamer\n");
        return;
    }

    if (pid == 0) {  // child
        char* argv[] = {
            "/home/alex/workspace/workspace/libuv/libuv/build/elevator/estreamer/estreamer",
            begin_str,
            end_str,
            url,
            NULL,
        };

        setenv("FTP_USERNAME", _options[OPTION_FIELD_FTP_USERNAME].value.string, 1);
        setenv("FTP_PASSWORD", _options[OPTION_FIELD_FTP_PASSWORD].value.string, 1);
        for (size_t i = 0; i < sizeof(argv) / sizeof(argv[0]); i++) {
            printf("%ld --> %s\n", i, argv[i]);
        }

        if (execvp(argv[0], argv /*, envp*/) < 0) {
            HR_LOGE("%s(%d): can not start:%s, %s\n", __FUNCTION__, __LINE__, argv[0], strerror(errno));
            exit(127);
        }

        HR_LOGD("child %s finished\n", argv[0]);
        exit(127);
    }

    HR_LOGD("this is parent process ....child:%d\n", pid);
}