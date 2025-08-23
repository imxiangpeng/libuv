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
#include <uv.h>

#include "cjson/cJSON.h"
#include "elevator.h"
#include "hr_list.h"
#include "hr_log.h"
#include "option.h"
#include "time_utils.h"
#include "uelevator.h"
#include "uviot.h"

#if ENABLE_RESCURE_BTN
#include <gpiod.h>

#define GPIO_CHIP_NAME "/dev/gpiochip0"
#define RESCURE_GPIO_PIN 6  // GPIOD_6
#endif

#define EVENT_FAULT_TOPIC_NAME "LiftFault"
#define LIFTFAULT_REPORT_EVENT_VIDEO_DURATION 20
#define LIFTFAULT_REPORT_EBIKE_VIDEO_DURATION 60
#define LIFTFAULT_REPORT_FAULT_VIDEO_MARGIN_SECONDS 120

// 困人事件处理流程：
// 1. 在 RESCURE_MODE_AUTO 模式，elevator_fault_occurred 被调用的时候
//    直接向平台发送困人事件，并且在结束的时候发送视频到 FTP
// 2. 在 RESCURE_MODE_MANUAL 模式，elevator_fault_occurred 会这是事件为 pending 事件，初始值为 1
//    然后启动 500ms 间隔的定时器 _people_trapped_fault_detect 来检测救援按钮是否按下
//    每次检测到按键就会将 pending 值 + 1， 如果达到 3 我们将确认故障发生，将事件推送给厚齐平台
// 3. 以上任意情况，在将事件发送给厚齐平台后，会启动间隔 1min 的定时器来检测事件发生时长，
//    当事件发生时长达到 90min 后，主动结束故障事件。
//    ==> 这里需要完善，不确认也需要 90min 超时检测，如果仍然是 pending 状态直接销毁
// 4. 我们使用了两个队列，正常非 pending 事件直接进入 _lift_fault_message_queue 等待 MQ发送，
//    在发送后，调度到 _lift_fault_idle_queue 队列，等待结束事件，
//    再次进入 _lift_fault_message_queue 发送后自动销毁
//    对于 pending 事件（手动救援模式下困人事件），首先进入 _lift_fault_idle_queue 等待确认，或者等待结束事件（pending事件不会发送）

#ifndef LIFTFAULT_KUNREN_AUTO_RESOLVED_TIMEOUT
#define LIFTFAULT_KUNREN_AUTO_RESOLVED_TIMEOUT (1000 * 60 * 90)  // 90min
#endif

// detect fault status:
// 1. kunren (manual mode) should wait gpio to confirm
// 2. kunren should be released after 90min
static uv_timer_t _rescure_timer;
static uv_timer_t _alive_timer;

// fault event only in memory do not save
struct lift_fault_event {
    enum elevator_exception type;
    int64_t fault_begin_time;  // please convert when you report
    int64_t fault_end_time;
    char* video_url;

    // manual mode: kunren should confirm with rescure button
    // kunren event should not report without confirm in manual mode
    int pending;

    struct hr_list_head entry;
};

static struct uviot* _iot = NULL;
// queue ready to publish
static HR_LIST_HEAD(_lift_fault_message_queue);
// pending fault which have publish but not finished
static HR_LIST_HEAD(_lift_fault_idle_queue);

static pthread_mutex_t _queue_lock;

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
    {.exception = ELEVATOR_EXCEPTION_DOOR_REPEATED, 0},
    {.exception = ELEVATOR_EXCEPTION_DOOR_CLOSE_ERROR, 0},
    {.exception = ELEVATOR_EXCEPTION_EBIKE, 0},
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
        // 11. 反复开关门
        case ELEVATOR_EXCEPTION_DOOR_REPEATED:
            return 11;
        // 12. 关门异常
        case ELEVATOR_EXCEPTION_DOOR_CLOSE_ERROR:
            return 12;
        // 202. 电瓶车
        case ELEVATOR_EXCEPTION_EBIKE:
            return 202;  // not in document
        case ELEVATOR_EXCEPTION_NONE:
        default:
            return 0;
    }
}
static int _on_publish(void** payload, int* len) {
    struct tm tm;
    time_t t;

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

    t = e->fault_begin_time / 1000;
    (void)localtime_r(&t, &tm);
    /*size_t size =*/strftime(tmp, sizeof(tmp), "%Y-%m-%d %H:%M:%S", &tm);
    cJSON_AddStringToObject(fault, "faultBeginTime", tmp);
    memset((void*)tmp, 0, sizeof(tmp));
    if (e->fault_end_time != 0) {
        t = e->fault_end_time / 1000;
        (void)localtime_r(&t, &tm);
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

    if (_options[OPTION_FAULT_REPORT_SWITCH].value.number == 0) {
        cJSON_Delete(root);
        return 0;
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
    .qos = 1,  // houqi not support Qos2
    .type = TOPIC_TYPE_PUBLISH,
    .callback.on_publish = _on_publish,
};

int topic_houqi_liftfault_init(struct uviot* iot, const char* public_key, const char* device_name) {
    (void)public_key;
    (void)device_name;
    _iot = iot;

    pthread_mutex_init(&_queue_lock, NULL);

    // init timer for sendvideo & sendsate command timeout
    memset((void*)&_rescure_timer, 0, sizeof(_rescure_timer));
    memset((void*)&_alive_timer, 0, sizeof(_alive_timer));
    uv_timer_init(uv_default_loop(), &_rescure_timer);
    uv_timer_init(uv_default_loop(), &_alive_timer);

    uviot_topic_register(iot, &dm_topic_liftfault);
    return 0;
}

int topic_houqi_liftfault_deinit(void) {
    if (_rescure_timer.type != UV_UNKNOWN_HANDLE) {
        if (!uv_is_closing((uv_handle_t*)&_rescure_timer)) {
            uv_close((uv_handle_t*)&_rescure_timer, NULL);
        }
    }
    if (_alive_timer.type != UV_UNKNOWN_HANDLE) {
        if (!uv_is_closing((uv_handle_t*)&_alive_timer)) {
            uv_close((uv_handle_t*)&_alive_timer, NULL);
        }
    }
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

// 考虑在这个函数中，将事件永久存储，应对重启或者断电情况
// 在这里还是在 publish 之后呢，因为这里还没有发送，丢也就丢了
static int publish_fault_event(struct lift_fault_event* e) {
    pthread_mutex_lock(&_queue_lock);
    hr_list_add_tail(&e->entry, &_lift_fault_message_queue);
    pthread_mutex_unlock(&_queue_lock);
    return uviot_publish_async(_iot, &dm_topic_liftfault);
}

// call this function when you have lcok
static int publish_fault_event_without_lock(struct lift_fault_event* e) {
    // pthread_mutex_lock(&_queue_lock);
    hr_list_add_tail(&e->entry, &_lift_fault_message_queue);
    // pthread_mutex_unlock(&_queue_lock);
    return uviot_publish_async(_iot, &dm_topic_liftfault);
}

#if ENABLE_RESCURE_BTN
// 0: no press
// 1: press
static int _people_trapped_fault_wait_rescure_button(void) {
    // low: press active
    return gpiod_ctxless_get_value(GPIO_CHIP_NAME, RESCURE_GPIO_PIN, true, "rescure-btn");
#if 0    
    int ret = 0;
    struct gpiod_chip* chip = gpiod_chip_open(GPIO_CHIP_NAME);
    if (!chip) {
        return 0;
    }

    struct gpiod_line* line = gpiod_chip_get_line(chip, RESCURE_GPIO_PIN);
    if (!line) {
        goto out;
    }
    if (gpiod_line_request_input(line, "rescure-btn") < 0) {
        goto out;
    }
    int val = gpiod_line_get_value(line);
    // val is 0 when pressed
    if (val == 0) {
        ret = 1;
    }  // < 0 or 0

out:
    if (line) {
        gpiod_line_release(line);
    }
    gpiod_chip_close(chip);
#endif
    return ret;
}
#endif

// filter out timeout event and auto resolve it
static void _fault_event_alive_timeout_detect() {
    struct lift_fault_event *e = NULL, *f = NULL;

    int64_t now = get_realtime_ms();
    int64_t begin = 0;
    // 1. when kunren fault is pending, we should detect rescure button
    pthread_mutex_lock(&_queue_lock);

    // 1.1 lookup kunren event
    hr_list_for_each_entry(f, &_lift_fault_idle_queue, entry) {
        begin = e->fault_begin_time;
        // timeout without confirmed
        if (now - begin >= LIFTFAULT_KUNREN_AUTO_RESOLVED_TIMEOUT) {
            HR_LOGD("%s(%d): fault:0x%X timeout pending:%d !\n", __FUNCTION__, __LINE__, e->type, e->pending);

            e->fault_end_time = get_realtime_ms();

            HR_LOGD("%s(%d): fault:0x%X timeout auto resolved!\n", __FUNCTION__, __LINE__, e->type);
            pthread_mutex_unlock(&_queue_lock);
            // elevator_fault_resolved will auto process pending event
            elevator_fault_resolved(e->type);
            pthread_mutex_lock(&_queue_lock);
        }
    }

    pthread_mutex_unlock(&_queue_lock);
}
// 1. detect rescure button when it's pending
// 2. detect timeout, then auto resolve fault
static void _people_trapped_fault_rescure_detect(uv_timer_t* handle) {
    (void)handle;
    struct lift_fault_event *e = NULL, *f = NULL;

    // 1. when kunren fault is pending, we should detect rescure button
    pthread_mutex_lock(&_queue_lock);

    // 1.1 lookup pending kunren event
    hr_list_for_each_entry(f, &_lift_fault_idle_queue, entry) {
        if (f->type == ELEVATOR_EXCEPTION_PEOPLE_TRAPPED && f->pending != 0) {
            e = f;
            break;
        }
    }

    if (!e) {
        pthread_mutex_unlock(&_queue_lock);
        if (uv_is_active((const uv_handle_t*)&_rescure_timer)) {
            uv_timer_stop(&_rescure_timer);
        }
        return;
    }

    HR_LOGD("%s(%d) fault:%d, pending:%d\n", __FUNCTION__, __LINE__, e->type, e->pending);

    // 2. the event is pending
    // detect rescure gpio button
#if ENABLE_RESCURE_BTN
    int pressed = _people_trapped_fault_wait_rescure_button();
    HR_LOGD("%s(%d) fault:%d, pending:%d, pressed:%d\n", __FUNCTION__, __LINE__, e->type, e->pending, pressed);
    if (pressed == 1) {
        e->pending++;
    }
#else
    e->pending++;  // no need, because pending is 0, never come in
#endif

    // kunren event has been confirmed from rescure button, fire it
    if (e->pending >= 3) {
        e->pending = 0;
        HR_LOGD("%s(%d) fault:%d, pending:%d, fire!!!\n", __FUNCTION__, __LINE__, e->type, e->pending);
        hr_list_del(&e->entry);
        pthread_mutex_unlock(&_queue_lock);
        publish_fault_event(e);
        return;
    }

    pthread_mutex_unlock(&_queue_lock);
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
        HR_LOGD("%s(%d): reset limit %d-%02d-%02d last %d-%02d-%02d...\n", __FUNCTION__, __LINE__,
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

    // not limit kunren & ebike
    if (ELEVATOR_EXCEPTION_PEOPLE_TRAPPED != fault && ELEVATOR_EXCEPTION_EBIKE != fault) {
        if (s->report_count >= _options[OPTION_FAULT_REPORT_LIMIT_PER_DAY].value.number) {
            HR_LOGD("%s(%d): fault:0x%X, reach report limit count:%d\n", __FUNCTION__, __LINE__, fault, s->report_count);
            return -1;
        }
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

    // mxp, 20250702, do not report & generate fault video when fault is disabled
    if (_options[OPTION_FAULT_REPORT_SWITCH].value.number == 0) {
        return 0;
    }

    e = fault_event_alloc();
    if (!e) {
        return -1;
    }

    // no save to persist storage
    s->report_count++;
    HR_LOGD("%s(%d): fault:0x%X, count:%d\n", __FUNCTION__, __LINE__, fault, s->report_count);
    e->type = fault;

    e->fault_begin_time = get_realtime_ms();

    e->pending = 0;

    // mxp, 20250822, kunren fault should be confirmed by rescure button in manual mode
    if (e->type == ELEVATOR_EXCEPTION_PEOPLE_TRAPPED) {
        HR_LOGD("%s(%d): fault:0x%X, rescure mode:%d\n", __FUNCTION__, __LINE__, fault, _options[OPTION_RESCURE_MODE].value.number);
        e->pending = _options[OPTION_RESCURE_MODE].value.number == RESCURE_MODE_AUTO ? 0 : 1;
        uv_timer_start(&_rescure_timer, _people_trapped_fault_rescure_detect, 500, 1);
    }

    // mxp, 20250707, broadcast fault event to system
    uelevator_send_fault_event(fault, 1);

    // only fanfukaiguanmen/guanmenyicang/kaimenxingti/ebike report in here
    // filter it in upload_fault_video
    if (e->type != ELEVATOR_EXCEPTION_PEOPLE_TRAPPED) {
        upload_fault_video(e);
    }

    if (e->pending == 0) {
        publish_fault_event(e);
    } else {
        pthread_mutex_lock(&_queue_lock);
        hr_list_add_tail(&e->entry, &_lift_fault_idle_queue);
        pthread_mutex_unlock(&_queue_lock);
    }
    // detect every second
    uv_timer_start(&_alive_timer, _fault_event_alive_timeout_detect, 1000 * 60, 1);
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

    // we should drop pending event
    if (e->pending != 0) {
        HR_LOGD("%s(%d): fault:0x%X is pending event, drop it\n", __FUNCTION__, __LINE__, fault);
        HR_INIT_LIST_HEAD(&e->entry);
        free(e);
        return 0;
    }

    // mxp, 20250702, do not report & generate fault video when fault is disabled
    if (_options[OPTION_FAULT_REPORT_SWITCH].value.number == 0) {
        HR_INIT_LIST_HEAD(&e->entry);
        free(e);
        return 0;
    }

    // mxp, 20250707, broadcast fault event to system
    uelevator_send_fault_event(fault, 0);

    // mxp, 20250620, people trapped video is upload when event is finished
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

// 困人录像上传路径\event_files\电梯编号\日期_时间.mp4
// 3个故障上传路径\fault_files\电梯编号\日期_时间.mp4
// 电瓶车检测上传路径\record\电梯编号\日期_时间.mp4
// 故障每天上报次数(自定义)：反复开关门、关门异常、开门行梯

// should convert utc timestamp to local timestamp
static void upload_fault_video(struct lift_fault_event* e) {
    struct tm tm;
    time_t t;

    char begin_str[64] = {0};
    char end_str[64] = {0};

    char url[LINE_MAX] = {0};

    char name[64] = {0};

    if (!e) {
        return;
    }

    memset((void*)&tm, 0, sizeof(tm));

    switch (e->type) {
        case ELEVATOR_EXCEPTION_RUN_WITHOUT_DOOR_CLOSED:
        // case ELEVATOR_EXCEPTION_STOPPED_NOT_AT_DOOR:
        // case ELEVATOR_EXCEPTION_RUN_OVER_TOP:
        // case ELEVATOR_EXCEPTION_RUN_OVER_BOTTOM:
        // case ELEVATOR_EXCEPTION_OVERSPEED:
        case ELEVATOR_EXCEPTION_DOOR_REPEATED:
        case ELEVATOR_EXCEPTION_DOOR_CLOSE_ERROR:
        case ELEVATOR_EXCEPTION_EBIKE:
            // report video 10 seconds around event

            t = e->fault_begin_time / 1000;
            (void)localtime_r(&t, &tm);
            /*size_t size =*/strftime(name, sizeof(name), "%Y%m%d_%H%M%S.mp4", &tm);

            memset((void*)&tm, 0, sizeof(tm));
            if (ELEVATOR_EXCEPTION_EBIKE == e->type) {
                t = e->fault_begin_time / 1000 - LIFTFAULT_REPORT_EBIKE_VIDEO_DURATION / 2;
            } else {
                t = e->fault_begin_time / 1000 - LIFTFAULT_REPORT_EVENT_VIDEO_DURATION / 2;
            }
            (void)localtime_r(&t, &tm);
            /*size_t size =*/strftime(begin_str, sizeof(begin_str), "%Y%m%d%H%M%S", &tm);

            memset((void*)&tm, 0, sizeof(tm));
            if (ELEVATOR_EXCEPTION_EBIKE == e->type) {
                t = e->fault_begin_time / 1000 + LIFTFAULT_REPORT_EBIKE_VIDEO_DURATION / 2;
            } else {
                t = e->fault_begin_time / 1000 + LIFTFAULT_REPORT_EVENT_VIDEO_DURATION / 2;
            }
            (void)localtime_r(&t, &tm);
            /*size_t size =*/strftime(end_str, sizeof(end_str), "%Y%m%d%H%M%S", &tm);

            if (e->type == ELEVATOR_EXCEPTION_EBIKE) {
                snprintf(url, sizeof(url), "%s/record/%s/%s", _options[OPTION_FTP_ADDRESS].value.string, elevator_deviceid(), name);
            } else {
                snprintf(url, sizeof(url), "%s/fault_files/%s/%s", _options[OPTION_FTP_ADDRESS].value.string, elevator_deviceid(), name);
            }
            break;

        case ELEVATOR_EXCEPTION_PEOPLE_TRAPPED:
            // report when finished
            if (e->fault_end_time == 0) {
                return;
            }
            t = e->fault_begin_time / 1000;
            (void)localtime_r(&t, &tm);
            /*size_t size =*/strftime(name, sizeof(name), "%Y%m%d_%H%M%S.mp4", &tm);

            memset((void*)&tm, 0, sizeof(tm));

            t = e->fault_begin_time / 1000 - LIFTFAULT_REPORT_FAULT_VIDEO_MARGIN_SECONDS;
            (void)localtime_r(&t, &tm);
            /*size_t size =*/strftime(begin_str, sizeof(begin_str), "%Y%m%d%H%M%S", &tm);

            memset((void*)&tm, 0, sizeof(tm));

            // 20250718, rescue must arrive within 30 minutes when trapping
            // we limit the duration of trapping video to 1h
            // we can save a significant amount of data traffic when trapping was incorrect triggered
            if ((e->fault_end_time - e->fault_begin_time) / 1000 + LIFTFAULT_REPORT_FAULT_VIDEO_MARGIN_SECONDS > 3600) {
                t = t + 3600;  // only keep 1h
            } else {
                t = e->fault_end_time / 1000 + LIFTFAULT_REPORT_FAULT_VIDEO_MARGIN_SECONDS;
            }

            (void)localtime_r(&t, &tm);
            /*size_t size =*/strftime(end_str, sizeof(end_str), "%Y%m%d%H%M%S", &tm);

            snprintf(url, sizeof(url), "%s/event_files/%s/%s", _options[OPTION_FTP_ADDRESS].value.string, elevator_deviceid(), name);
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
            "/usr/bin/estreamer",
            begin_str,
            end_str,
            url,
            NULL,
        };

        setenv("FTP_USERNAME", _options[OPTION_FTP_USERNAME].value.string, 1);
        setenv("FTP_PASSWORD", _options[OPTION_FTP_PASSWORD].value.string, 1);
        for (size_t i = 0; i < sizeof(argv) / sizeof(argv[0]); i++) {
            printf("%ld --> %s\n", i, argv[i]);
        }

        if (execvp(argv[0], argv) < 0) {
            HR_LOGE("%s(%d): can not start:%s, %s\n", __FUNCTION__, __LINE__, argv[0], strerror(errno));
            exit(127);
        }

        HR_LOGD("child %s finished\n", argv[0]);
        exit(127);
    }

    HR_LOGD("this is parent process ....child:%d\n", pid);
}