// mxp, 20250522, implement houqi topic: /API/V1/Up/LiftFault

#define _GNU_SOURCE
#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <uuid/uuid.h>
#include <uv.h>

#include "cjson/cJSON.h"
#include "elevator.h"
#include "file_util.h"
#include "hr_list.h"
#include "hr_log.h"
#include "option.h"
#include "time_utils.h"
#include "uelevator.h"
#include "uviot.h"

// mxp, 20251230, 调整手动困人模式下检测逻辑
// 由于其他模块采用中断方式检测 GPIO，所以这里我们无法再检测 GPIO 事件了。
//
// 原来的手动困人模式由内部检测 GPIO 调整为：
//
// 1. 内部仍然以 pending 状态保存事件
// 2. 其他模块接收到 elevator.event.x.fault '{"type":"kunren", "status":1}' 事件后等待按键
// 3. 在其他模块检测到按键后，调用 ubus send elevator.event.x.fault '{"type":"kunren", "status":2}' 来直接触发立即发送故障
// 4. 我们在 elevator_fault_occurred 中会检测 pending 状态的困人事件，如果存在那么将其立马发送出去

#if ENABLE_RESCUE_BTN
#include <gpiod.h>

#define GPIO_CHIP_NAME "/dev/gpiochip0"
#define RESCUE_GPIO_PIN 6  // GPIOD_6
#endif

#define EVENT_FAULT_TOPIC_NAME "LiftFault"
#define LIFTFAULT_REPORT_EVENT_VIDEO_DURATION 20
#define LIFTFAULT_REPORT_EBIKE_VIDEO_DURATION 60
#define LIFTFAULT_REPORT_FAULT_VIDEO_MARGIN_SECONDS 120

// mxp, 20250928, 延迟发送故障事件
// 因为识别以及算法等问题可能存在一些误报，我们计划允许一些容错，
// 在指定时间窗口内发生并且结束了，我们就不上报这些事件
// 以开门走车为例，我们发现加速度识别停止可能存在缓慢情况，
// 导致出现，电梯已经停止了，但是加速度速度还不为 0 的情况
// 在 elevatord 中我们通过气压强制对加速度归零，但是也有 2s 窗口
// 所以，这里我们倾向于采用时间窗口确认机制来规避一些问题
#define USE_LIFTFAULT_CONFIRM_TIME 5000  // pending 5s before send fault

// 困人事件处理流程：
// 1. 在 RESCUE_MODE_AUTO 模式，elevator_fault_occurred 被调用的时候
//    直接向平台发送困人事件，并且在结束的时候发送视频到 FTP
// 2. 在 RESCUE_MODE_MANUAL 模式，elevator_fault_occurred 会这是事件为 pending 事件，初始值为 1
//    然后启动 500ms 间隔的定时器 _people_trapped_fault_detect 来检测救援按钮是否按下
//    每次检测到按键就会将 pending 值 + 1， 如果达到 3 我们将确认故障发生，将事件推送给厚齐平台
// 3. 在任意故障发生后，会启动间隔 1min 的定时器来检测事件发生时长，
//    当事件发生时长达到 150min 后，主动结束故障事件。
//    ==> 这里需要完善，不确认也需要 150min 超时检测，如果仍然是 pending 状态直接销毁
//    ==> 中国超时逻辑适用于所有的事件
// 4. 我们使用了两个队列，正常非 pending 事件直接进入 _lift_fault_message_queue 等待 MQ发送，
//    在发送后，调度到 _lift_fault_idle_queue 队列，等待结束事件，
//    再次进入 _lift_fault_message_queue 发送后自动销毁
//    对于 pending 事件（手动救援模式下困人事件），首先进入 _lift_fault_idle_queue 等待确认，或者等待结束事件（pending事件不会发送）
// 5. 困人的实际检测实在 state_machine 里面，我们在内部释放了事件，但是在外部其仍然是出于困人的状态，实际上电梯也仍然出于困人状态, 等待解除
//    只是在解除时因为之前已经上报过解除事件，所以这里不会再做什么了。
//    在模拟测试的时候一定要注意这一点。

#ifndef LIFTFAULT_FAULT_AUTO_RESOLVED_TIMEOUT
#define LIFTFAULT_FAULT_AUTO_RESOLVED_TIMEOUT (1000 * 60 * 150)  // 150min
#endif

// 后来修改也用这个定时器来监控 pending 事件（非困人事件）
// 当时间超过 USE_LIFTFAULT_CONFIRM_TIME 的时候自动派发，
// 在此时间内结束的事件不会派发
#ifndef LIFTFAULT_FAULT_AUTO_RESOLVED_DETECT_TIMEOUT
#define LIFTFAULT_FAULT_AUTO_RESOLVED_DETECT_TIMEOUT (1000)  // detect every seconds
#endif

#ifndef LIFTFAULT_FAULT_RESCUE_BTN_DETECT_TIMEOUT
#define LIFTFAULT_FAULT_RESCUE_BTN_DETECT_TIMEOUT 300
#endif

#ifndef LIFTFAULT_FAULT_RESCUE_PENDING_MAX
// initial pending 1,  3 - 1 = 2 * 300 ms
#define LIFTFAULT_FAULT_RESCUE_PENDING_MAX 3
#endif

#define DUMP_FAULT_QUEUE_EVENTS()                                                           \
    do {                                                                                    \
        struct lift_fault_event* e = NULL;                                                  \
        HR_LOGD("%s(%d) queue begin:\n", __func__, __LINE__);                               \
        hr_list_for_each_entry(e, &_lift_fault_message_queue, entry) {                      \
            HR_LOGD("-> message: %p, type:0x%x -> %s, pending:%d, begin:%ld\n",             \
                    e, e->type, fault_to_string(e->type), e->pending, e->fault_begin_time); \
        }                                                                                   \
        hr_list_for_each_entry(e, &_lift_fault_idle_queue, entry) {                         \
            HR_LOGD("-> idle: %p, type:0x%x -> %s, pending:%d, begin:%ld\n",                \
                    e, e->type, fault_to_string(e->type), e->pending, e->fault_begin_time); \
        }                                                                                   \
        HR_LOGD("%s(%d) queue end!\n", __func__, __LINE__);                                 \
    } while (0)

// detect fault status:
// 1. kunren (manual mode) should wait gpio to confirm
// 2. kunren should be released after 90min
#if ENABLE_RESCUE_BTN
static uv_timer_t _rescue_btn_timer;
#endif
static uv_timer_t _alive_timer;

// fault event only in memory do not save
struct lift_fault_event {
    enum elevator_exception type;
    int64_t fault_begin_time;  // please convert when you report
    int64_t fault_end_time;
    char* video_url;

    // manual mode: kunren should confirm with rescue button
    // kunren event should not report without confirm in manual mode
    int pending;

    struct hr_list_head entry;
};

static struct uviot* _iot = NULL;
// queue ready to publish
static HR_LIST_HEAD(_lift_fault_message_queue);
// pending fault which have publish but not finished
static HR_LIST_HEAD(_lift_fault_idle_queue);

// most time we are running in uv loop thread,
// but ebike/speed occure in uloop thread
// mxp, 20259826, remove mutex lock,
// all fault will be report from statemachine,
// which schedule event to uv loop thread
// static pthread_mutex_t _queue_lock;

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

static uint32_t _exception_indicator = ELEVATOR_EXCEPTION_NONE;

// date to record and reset report statistics
static struct tm _fault_report_statistics_tm;

static struct uviot_topic _topic_liftfault;

static void upload_fault_video(struct lift_fault_event* e);

static void _fault_event_alive_timeout_detect(uv_timer_t* handle);
static void _store_fault_event(void);
static void _restore_fault_event(void);

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

    DUMP_FAULT_QUEUE_EVENTS();

    if (hr_list_empty(&_lift_fault_message_queue)) {
        cJSON_Delete(root);
        return -1;
    }

    e = hr_list_first_entry(&_lift_fault_message_queue, struct lift_fault_event, entry);

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
    cJSON_AddNumberToObject(root, "runningDirection", st.direction == ELEVATOR_DIR_STATIONARY ? 3 : st.direction);
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

    // init suffix of video path
    memset((void*)tmp, 0, sizeof(tmp));
    // only set when upload is enabled && ftp is valid
    if (0 != _options[OPTION_FAULT_VIDEO_UPLOAD_SWITCH].value.number &&
        NULL != _options[OPTION_FTP_ADDRESS].value.string) {
        switch (e->type) {
            case ELEVATOR_EXCEPTION_RUN_WITHOUT_DOOR_CLOSED:
            case ELEVATOR_EXCEPTION_DOOR_REPEATED:
            case ELEVATOR_EXCEPTION_DOOR_CLOSE_ERROR: {
                t = e->fault_begin_time / 1000;
                (void)localtime_r(&t, &tm);
                int c = snprintf(tmp, sizeof(tmp), "/fault_files/%s/", elevator_deviceid());
                if (c > 0) {
                    /*size_t size =*/strftime(tmp + c, sizeof(tmp) - c, "%Y%m%d_%H%M%S.mp4", &tm);
                }
                break;
            }
            case ELEVATOR_EXCEPTION_EBIKE: {
                t = e->fault_begin_time / 1000;
                (void)localtime_r(&t, &tm);
                int c = snprintf(tmp, sizeof(tmp), "/record/%s/", elevator_deviceid());
                if (c > 0) {
                    /*size_t size =*/strftime(tmp + c, sizeof(tmp) - c, "%Y%m%d_%H%M%S.mp4", &tm);
                }
                break;
            }

            case ELEVATOR_EXCEPTION_PEOPLE_TRAPPED: {
                // report when finished
                t = e->fault_begin_time / 1000;
                (void)localtime_r(&t, &tm);
                int c = snprintf(tmp, sizeof(tmp), "/event_files/%s/", elevator_deviceid());
                if (c > 0) {
                    /*size_t size =*/strftime(tmp + c, sizeof(tmp) - c, "%Y%m%d_%H%M%S.mp4", &tm);
                }
                break;
            }
            default:
                break;
        }
    }

    cJSON_AddStringToObject(fault, "faultVideoUrl", tmp);

    // take off from list
    hr_list_del(&e->entry);

    // the fault have finished, release it
    if (e->fault_end_time == 0) {
        // we should keep it for finish event
        hr_list_add_tail(&e->entry, &_lift_fault_idle_queue);
    } else {
        // now we can free event
        HR_INIT_LIST_HEAD(&e->entry);
        free(e);
    }

    _store_fault_event();

    DUMP_FAULT_QUEUE_EVENTS();

    // mxp, 20250828, uv_async maybe union multi operations
    // we should schedule again when message queue not empty
    if (!hr_list_empty(&_lift_fault_message_queue)) {
        uviot_publish_async(_iot, &_topic_liftfault);
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

static struct uviot_topic _topic_liftfault = {
    .name = EVENT_FAULT_TOPIC_NAME,
    .topic = "/API/V1/Up/" EVENT_FAULT_TOPIC_NAME,
    .period = 0,
    .qos = 1,  // HQ not support QoS 2
    .type = TOPIC_TYPE_PUBLISH,
    .callback.on_publish = _on_publish,
};

int topic_houqi_liftfault_init(struct uviot* iot, const char* public_key, const char* device_name) {
    (void)public_key;
    (void)device_name;
    _iot = iot;

#if ENABLE_TOPIC_CUSTOM
    // if (!_options[OPTION_MQ_TOPIC_PUB_LIFTFAULT].value.string) {
    //    HR_LOGE("invalid liftfault topic ...\n");
    //    _exit(-1);
    //}
    if (_options[OPTION_MQ_TOPIC_PUB_LIFTFAULT].value.string) {
        snprintf(_topic_liftfault.topic, sizeof(_topic_liftfault.topic), "%s", _options[OPTION_MQ_TOPIC_PUB_LIFTFAULT].value.string);
    }
#endif

    _restore_fault_event();

    // init timer for sendvideo & sendsate command timeout
#if ENABLE_RESCUE_BTN
    memset((void*)&_rescue_btn_timer, 0, sizeof(_rescue_btn_timer));
    uv_timer_init(uv_default_loop(), &_rescue_btn_timer);
#endif

    memset((void*)&_alive_timer, 0, sizeof(_alive_timer));
    uv_timer_init(uv_default_loop(), &_alive_timer);

    // auto start alive timer when there is any idle which maybe resumed
    if (!hr_list_empty(&_lift_fault_idle_queue)) {
        uv_timer_start(&_alive_timer, _fault_event_alive_timeout_detect, LIFTFAULT_FAULT_AUTO_RESOLVED_DETECT_TIMEOUT, LIFTFAULT_FAULT_AUTO_RESOLVED_DETECT_TIMEOUT);
    }

    uviot_topic_register(iot, &_topic_liftfault);
    return 0;
}

int topic_houqi_liftfault_deinit(void) {
#if ENABLE_RESCUE_BTN
    if (_rescue_btn_timer.type != UV_UNKNOWN_HANDLE) {
        if (!uv_is_closing((uv_handle_t*)&_rescue_btn_timer)) {
            uv_close((uv_handle_t*)&_rescue_btn_timer, NULL);
        }
    }
#endif
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
#if 0
static void fault_event_free(struct lift_fault_event* e) {
    if (!e) {
        return;
    }

    hr_list_del(&e->entry);

    HR_INIT_LIST_HEAD(&e->entry);

    free(e);
}
#endif

// 考虑在这个函数中，将事件永久存储，应对重启或者断电情况
// 在这里还是在 publish 之后呢，因为这里还没有发送，丢也就丢了
static int publish_fault_event(struct lift_fault_event* e) {
    hr_list_add_tail(&e->entry, &_lift_fault_message_queue);
    return uviot_publish_async(_iot, &_topic_liftfault);
}

// filter out timeout event and auto resolve it
static void _fault_event_alive_timeout_detect(uv_timer_t* handle) {
    (void)handle;
    struct lift_fault_event *e = NULL, *n = NULL;

    int64_t now = get_realtime_ms();
    int64_t begin = 0;

begin:

    // can we stop timer? it maybe reenter idle from message queue
    if (hr_list_empty(&_lift_fault_idle_queue) && hr_list_empty(&_lift_fault_message_queue)) {
        HR_LOGD("%s(%d): all queues are empty, stop timer!\n", __FUNCTION__, __LINE__);
        // timer should be stopped when both idle and message queues are empty
        uv_timer_stop(handle);
        return;
    }

    // 1.1 lookup kunren event
    hr_list_for_each_entry_safe(e, n, &_lift_fault_idle_queue, entry) {
        begin = e->fault_begin_time;

        HR_LOGD("%s(%d): fault:0x%X -> %s timeout pending:%d elapse:%ld!\n", __FUNCTION__, __LINE__, e->type, fault_to_string(e->type), e->pending, now - begin);

#if USE_LIFTFAULT_CONFIRM_TIME
        if (e->type != ELEVATOR_EXCEPTION_PEOPLE_TRAPPED) {
            if (e->pending != 0) {
                // timeout auto confirm the event send it
                if (now - begin >= USE_LIFTFAULT_CONFIRM_TIME) {
                    e->pending = 0;

                    hr_list_del(&e->entry);
                    publish_fault_event(e);
                    upload_fault_video(e);  // no trapped
                    continue;
                }
            }
        }
#endif
        // timeout without confirmed
        if (now - begin >= LIFTFAULT_FAULT_AUTO_RESOLVED_TIMEOUT) {
            HR_LOGD("%s(%d): fault:0x%X -> %s timeout pending:%d auto resolved!\n", __FUNCTION__, __LINE__, e->type, fault_to_string(e->type), e->pending);

            e->fault_end_time = get_realtime_ms();

            // it's in same idle queue, directly call elevator_fault_resolved
            // elevator_fault_resolved will auto process pending event
            elevator_fault_resolved(e->type);
            // or we can use hr_list_for_each_entry_safe
            // we not use hr_list_for_each_entry_safe because elevator_fault_resolved accept type will loop again
            // we should add new function directly fire and release e not use type
            goto begin;
        }
    }
}

#if ENABLE_RESCUE_BTN
// 0: no press
// 1: press
static int _people_trapped_fault_wait_rescue_button(void) {
    // low: press active
    return gpiod_ctxless_get_value(GPIO_CHIP_NAME, RESCUE_GPIO_PIN, true, "rescue-btn");
#if 0    
    int ret = 0;
    struct gpiod_chip* chip = gpiod_chip_open(GPIO_CHIP_NAME);
    if (!chip) {
        return 0;
    }

    struct gpiod_line* line = gpiod_chip_get_line(chip, RESCUE_GPIO_PIN);
    if (!line) {
        goto out;
    }
    if (gpiod_line_request_input(line, "rescue-btn") < 0) {
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

#if ENABLE_RESCUE_BTN
// 1. detect rescue button when it's pending
static void _people_trapped_fault_rescue_btn_detect(uv_timer_t* handle) {
    (void)handle;
    struct lift_fault_event *e = NULL, *f = NULL;

    int pressed = 0;
#if ENABLE_RESCUE_BTN
    // detect button without lock
    pressed = _people_trapped_fault_wait_rescue_button();
#endif

    // 1. lookup pending kunren event
    hr_list_for_each_entry(f, &_lift_fault_idle_queue, entry) {
        if (f->type == ELEVATOR_EXCEPTION_PEOPLE_TRAPPED && f->pending != 0) {
            e = f;
            break;
        }
    }

    if (!e) {
        uv_timer_stop(&_rescue_btn_timer);
        return;
    }

    HR_LOGD("%s(%d) fault:%d, pending:%d, pressed:%d\n", __FUNCTION__, __LINE__, e->type, e->pending, pressed);

    if (pressed != 1) {
        return;
    }
    // 2. the event is pending
    // detect rescue gpio button
    e->pending++;

    // kunren event has been confirmed from rescue button, fire it
    if (e->pending >= LIFTFAULT_FAULT_RESCUE_PENDING_MAX) {
        e->pending = 0;
        HR_LOGD("%s(%d) fault:0x%x -> %s, pending:%d, fire!!!\n", __FUNCTION__, __LINE__, e->type, fault_to_string(e->type), e->pending);
        // delete from idle and queue into message
        hr_list_del(&e->entry);
        publish_fault_event(e);
        return;
    }
}
#endif

int elevator_fault_occurred(enum elevator_exception fault, int immediate) {
    struct lift_fault_event* e = NULL;
    struct fault_report_statistics* s = NULL;

    struct tm tm;
    struct timespec ts;

    clock_gettime(CLOCK_REALTIME, &ts);
    (void)localtime_r(&ts.tv_sec, &tm);

    HR_LOGD("%s(%d): fault:0x%X -> %s\n", __FUNCTION__, __LINE__, fault, fault_to_string(fault));

    _exception_indicator |= fault;

    DUMP_FAULT_QUEUE_EVENTS();

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
        HR_LOGD("%s(%d): statistics not support fault:0x%X -> %s\n", __FUNCTION__, __LINE__, fault, fault_to_string(fault));
        return -1;
    }

    // not limit kunren & ebike
    if (ELEVATOR_EXCEPTION_PEOPLE_TRAPPED != fault && ELEVATOR_EXCEPTION_EBIKE != fault) {
        if (s->report_count >= _options[OPTION_FAULT_REPORT_LIMIT_PER_DAY].value.number) {
            HR_LOGD("%s(%d): fault:0x%X -> %s, reach report limit count:%d\n", __FUNCTION__, __LINE__, fault, fault_to_string(fault), s->report_count);
            return -1;
        }
    }

    if (!hr_list_empty(&_lift_fault_message_queue)) {
        struct lift_fault_event* f = NULL;
        hr_list_for_each_entry(f, &_lift_fault_message_queue, entry) {
            if (f->type == fault) {
                HR_LOGE("%s(%d): fault:0x%X -> %s is occurring and ready to report, do not report again\n", __FUNCTION__, __LINE__, fault, fault_to_string(fault));
                return -1;
            }
        }
    }

    if (!hr_list_empty(&_lift_fault_idle_queue)) {
        struct lift_fault_event* f = NULL;
        hr_list_for_each_entry(f, &_lift_fault_idle_queue, entry) {
            if (f->type == fault) {
                HR_LOGE("%s(%d): fault:0x%X -> %s is occurring, do not report again\n", __FUNCTION__, __LINE__, fault, fault_to_string(fault));

#if !ENABLE_RESCUE_BTN
                HR_LOGE("%s(%d): fault:0x%X -> %s, pending: %d, immediate:%d\n", __FUNCTION__, __LINE__, fault, fault_to_string(fault), e->pending, immediate);
                if (immediate == 1 && e->type == ELEVATOR_EXCEPTION_PEOPLE_TRAPPED && e->pending != 0) {
                    // button detect has been moved to other modules
                    // it will trigger fault occurr using immediate == 1
                    // so we directly send the pending event when we receive immediate fault
                    // delete from idle and queue into message
                    e->pending = 0;
                    hr_list_del(&e->entry);
                    publish_fault_event(e);
                    return 0;
                }
#endif
                return -1;
            }
        }
    }

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
    HR_LOGD("%s(%d): fault:0x%X -> %s, count:%d\n", __FUNCTION__, __LINE__, fault, fault_to_string(fault), s->report_count);
    e->type = fault;

    e->fault_begin_time = get_realtime_ms();

#if USE_LIFTFAULT_CONFIRM_TIME
    e->pending = 1;
#else
    e->pending = 0;
#endif

    // mxp, 20250822, kunren fault should be confirmed by rescue button in manual mode
    if (e->type == ELEVATOR_EXCEPTION_PEOPLE_TRAPPED) {
        HR_LOGD("%s(%d): fault:0x%X -> %s, rescue mode:%ld\n", __FUNCTION__, __LINE__, fault, fault_to_string(fault), _options[OPTION_RESCUE_MODE].value.number);
        e->pending = _options[OPTION_RESCUE_MODE].value.number == RESCUE_MODE_AUTO ? 0 : 1;
    }

    //// only fanfukaiguanmen/guanmenyicang/kaimenxingti/ebike report in here
    //// filter it in upload_fault_video
    // if (e->type != ELEVATOR_EXCEPTION_PEOPLE_TRAPPED) {
    //     upload_fault_video(e);
    // }

    // we should report directly
    if (immediate == 1) {
        e->pending = 0;
    }

    if (e->pending == 0) {
        // only fanfukaiguanmen/guanmenyicang/kaimenxingti/ebike report in here
        // filter it in upload_fault_video
        if (e->type != ELEVATOR_EXCEPTION_PEOPLE_TRAPPED) {
            upload_fault_video(e);
        }
        publish_fault_event(e);
    } else {
        hr_list_add_tail(&e->entry, &_lift_fault_idle_queue);

#if ENABLE_RESCUE_BTN
        // only support ELEVATOR_EXCEPTION_PEOPLE_TRAPPED
        if (e->type == ELEVATOR_EXCEPTION_PEOPLE_TRAPPED) {
            uv_timer_start(&_rescue_btn_timer, _people_trapped_fault_rescue_btn_detect, LIFTFAULT_FAULT_RESCUE_BTN_DETECT_TIMEOUT, LIFTFAULT_FAULT_RESCUE_BTN_DETECT_TIMEOUT);
        }
#endif
    }

    // detect every second
    if (!uv_is_active((const uv_handle_t*)&_alive_timer)) {
        uv_timer_start(&_alive_timer, _fault_event_alive_timeout_detect, LIFTFAULT_FAULT_AUTO_RESOLVED_DETECT_TIMEOUT, LIFTFAULT_FAULT_AUTO_RESOLVED_DETECT_TIMEOUT);
    }

    // mxp, 20250707, broadcast fault event to system
    uelevator_send_fault_event(fault, 1);

    DUMP_FAULT_QUEUE_EVENTS();
    return 0;
}

// the same fault can not report more than once, before it end
// pending event will not report
int elevator_fault_resolved(enum elevator_exception fault) {
    struct lift_fault_event *e = NULL, *f = NULL;
    // we should lookup in idle list
    // ignore when can not find
    HR_LOGD("%s(%d): fault:0x%X\n", __FUNCTION__, __LINE__, fault);

    _exception_indicator &= ~fault;

    DUMP_FAULT_QUEUE_EVENTS();
    // drop event in message queue
    hr_list_for_each_entry(f, &_lift_fault_message_queue, entry) {
        if (f->type == fault) {
            e = f;
            break;
        }
    }

    if (e) {
        HR_LOGD("%s(%d): found fault:0x%X, in message queue!\n", __FUNCTION__, __LINE__, fault);
        // take off from idle queue
        hr_list_del(&e->entry);

        // we should drop pending event
        HR_LOGD("%s(%d): fault:0x%X in message queue, drop it\n", __FUNCTION__, __LINE__, fault);
        HR_INIT_LIST_HEAD(&e->entry);
        free(e);
        DUMP_FAULT_QUEUE_EVENTS();
        return 0;
    }

    if (hr_list_empty(&_lift_fault_idle_queue)) {
        HR_LOGD("%s(%d): fault queue empty fault:0x%X\n", __FUNCTION__, __LINE__, fault);
        return -1;
    }

    e = NULL;

    hr_list_for_each_entry(f, &_lift_fault_idle_queue, entry) {
        if (f->type == fault) {
            e = f;
            break;
        }
    }

    if (!e) {
        HR_LOGD("%s(%d): not found fault:0x%X\n", __FUNCTION__, __LINE__, fault);
        return -1;
    }

    // take off from idle queue
    hr_list_del(&e->entry);

    HR_LOGD("take off from idle queue ...\n");
    DUMP_FAULT_QUEUE_EVENTS();

    e->fault_end_time = get_realtime_ms();

    // mxp, 20250702, do not report & generate fault video when fault is disabled
    if (_options[OPTION_FAULT_REPORT_SWITCH].value.number == 0) {
        HR_INIT_LIST_HEAD(&e->entry);
        free(e);
        return 0;
    }

    // mxp, 20250707, broadcast fault event to system
    uelevator_send_fault_event(fault, 0);

    // mxp, 20251218, workaround force send ubus fault event
    // other modules depend it
    // we should drop pending event
    if (e->pending != 0) {
        HR_LOGD("%s(%d): fault:0x%X is pending event, drop it\n", __FUNCTION__, __LINE__, fault);
        HR_INIT_LIST_HEAD(&e->entry);
        free(e);
        return 0;
    }

    // mxp, 20250620, people trapped video is upload when event is finished
    if (e->type == ELEVATOR_EXCEPTION_PEOPLE_TRAPPED) {
        upload_fault_video(e);
    }

    publish_fault_event(e);
    DUMP_FAULT_QUEUE_EVENTS();
    return 0;
}

int elevator_fault_is_active(enum elevator_exception fault) {
    return _exception_indicator & fault;
}

int elevator_fault_review(int* type, uint64_t* occurred_ms) {
    struct lift_fault_event* e = NULL;

    if (!type || !occurred_ms) {
        return -1;
    }

    if (hr_list_empty(&_lift_fault_idle_queue)) {
        return 0;
    }

    e = hr_list_first_entry(&_lift_fault_idle_queue, struct lift_fault_event, entry);
    *type = to_houqi_fault(e->type);
    *occurred_ms = e->fault_begin_time;

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

    if (0 == _options[OPTION_FAULT_VIDEO_UPLOAD_SWITCH].value.number) {
        HR_LOGE("fault video upload function is disabled!\n");
        return;
    }

    if (!_options[OPTION_FTP_ADDRESS].value.string) {
        HR_LOGE("%s(%d): no valid upload address\n", __func__, __LINE__);
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
            "-s",  // use persist task, which will continue even power off or reboot
            begin_str,
            end_str,
            url,
            NULL,
        };

        if (_options[OPTION_FTP_USERNAME].value.string) {
            setenv("FTP_USERNAME", _options[OPTION_FTP_USERNAME].value.string, 1);
        } else {
            setenv("FTP_USERNAME", "anonymous", 1);
        }
        if (_options[OPTION_FTP_PASSWORD].value.string) {
            setenv("FTP_PASSWORD", _options[OPTION_FTP_PASSWORD].value.string, 1);
        }

        for (size_t i = 0; i < sizeof(argv) / sizeof(argv[0]); i++) {
            printf("%ld --> %s\n", i, argv[i]);
            printf("%ld --> %s\n", i, argv[i]);
        }

        // adjust child process's adj
        FILE* fp = fopen("/proc/self/oom_score_adj", "w");
        if (fp) {
            fwrite("0", 1, 1, fp);
            fclose(fp);
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

// we should store unresolved faults(idle queue)
// only care events which dequeue from message and queue idle
// so we not track pending in idle queue before it enter message
//  _store_fault_event only when event occurred and published
// store idle queue using binary format
// |size(uint64)|struct lift_fault_event|...|
// but you should reinit list
static void _store_fault_event(void) {
    size_t size = 0;
    int fd = -1;

    char* tmp = NULL;
    int tmp_len = 0;
    char* last_slash = NULL;

    const char* TMPFILE_TEMPLATE = "tmp_XXXXXX";

    struct lift_fault_event* e = NULL;

    if (hr_list_empty(&_lift_fault_idle_queue)) {
        unlink(FAULT_HISTORICAL);
        sync();
        return;
    }

    tmp_len = strlen(FAULT_HISTORICAL) + strlen(TMPFILE_TEMPLATE) + 1;  // + '\0'

    tmp = (char*)calloc(1, tmp_len);  // hardcode 8(.XXXXXX + \0)
    if (!tmp) {
        return;
    }

    snprintf(tmp, tmp_len, "%s%s", FAULT_HISTORICAL, TMPFILE_TEMPLATE);
    fd = mkostemp(tmp, O_RDWR | O_TRUNC | O_CREAT);
    if (fd < 0) {
        free(tmp);
        return;
    }

    fchmod(fd, S_IRUSR | S_IWUSR | S_IRGRP);

    // skip size
    lseek(fd, sizeof(size), SEEK_SET);

    hr_list_for_each_entry(e, &_lift_fault_idle_queue, entry) {
        if (e->pending != 0) {
            HR_LOGD("skip pending event\n");
            continue;
        }
        size++;
        futil_write_fd(fd, (char*)e, sizeof(*e));
    }

    lseek(fd, 0, SEEK_SET);
    printf("length :%ld\n", size);
    futil_write_fd(fd, (char*)&size, sizeof(size));

    fdatasync(fd);
    close(fd);

    rename(tmp, FAULT_HISTORICAL);

    last_slash = strrchr(tmp, '/');

    if (!last_slash) {
        strcpy(tmp, ".");
    } else {
        *(last_slash + 1) = '\0';
    }

    printf("parent directory:%s\n", tmp);
    fd = open(tmp, O_RDONLY | O_DIRECTORY);
    if (fd != -1) {
        fsync(fd);
        close(fd);
    }

    free(tmp);
}
static void _restore_fault_event(void) {
    struct tm tm;
    char tmp[256] = {0};

    size_t size = 0;

    char* data = NULL;
    ssize_t len = futil_read(FAULT_HISTORICAL, &data);
    if (len <= 0 || !data) {
        return;
    }

    // do not unlink historical file until publish

    size = *(size_t*)data;

    printf("size :%ld\n", size);

    if (len - sizeof(size_t) != size * sizeof(struct lift_fault_event)) {
        free(data);
        // invalid drop
        unlink(FAULT_HISTORICAL);
        sync();
        return;
    }

    for (size_t i = 0; i < size; i++) {
        struct lift_fault_event* s = (struct lift_fault_event*)(data + sizeof(size));
        struct lift_fault_event* e = fault_event_alloc();
        if (e) {
            e->type = (s + i)->type;
            e->fault_begin_time = (s + i)->fault_begin_time;
            e->fault_end_time = (s + i)->fault_end_time;
            e->pending = (s + i)->pending;

            time_t t = e->fault_begin_time / 1000;
            (void)localtime_r(&t, &tm);
            /*size_t size =*/strftime(tmp, sizeof(tmp), "%Y-%m-%d %H:%M:%S", &tm);

            HR_LOGD("resume event: 0x%x -> %s, begin:%s\n", e->type, fault_to_string(e->type), tmp);
            printf("resume event: 0x%x -> %s, begin:%s\n", e->type, fault_to_string(e->type), tmp);
            _exception_indicator |= e->type;
            hr_list_add_tail(&e->entry, &_lift_fault_idle_queue);
        }
    }
    free(data);
}
