// mxp, 20250501, ubus interface obtains data by communicating with the uelevatord module
// also subscribe some broadcast events such ubus.object.* and elevator.event.*

#include "uelevator.h"

#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>

#include "elevator.h"
#include "hr_buffer.h"
#include "hr_log.h"
#include "libubox/blob.h"
#include "libubox/blobmsg.h"
#include "libubox/blobmsg_json.h"
#include "libubus.h"
#include "sconf.h"
#include "state_machine.h"
#include "time_utils.h"

#define ELEVATORD_NAME "elevatord"
#define ELEVATOR_EVENT_PREFIX "elevator.event."
#define UBUS_SOCK "/tmp/ubus.sock"

#define _UBUS_RETRY_TIMEOUT (2)

#define ELEVATOR_SPEED_THRESHOLD 3.1

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

enum {
    MSG_REALTIME,
    MSG_HISTORICAL,
    MSG_QUIT
};

// 定于事件
// RealTime
// Historical
#define ELEVATORD_EVENT_REALTIME "RealTime"
#define ELEVATORD_EVENT_HISTORICAL "Historical"
#define ELEVATORD_EVENT_MOTION "Motion"

static struct ubus_context* _ubus_ctx = NULL;

static pthread_t _uobject_tid = 0;
static int _request_exit = 0;
static int _pipefd[2] = {-1, -1};  // [0]=read, [1]=write

static struct ubus_subscriber _elevatord_subscriber;
static uint32_t _elevatord_object_id = 0;
static struct blob_buf _b;

static struct elevator_status _status = {.door_state = ELEVATOR_DOOR_CLOSE};

static struct elevator_historical _historical;

static uint32_t _elevator_exception = ELEVATOR_EXCEPTION_NONE;
static struct sconf_proto _speed_limit_threhold = {"SPEED_LIMIT_THREHOLD", PROTO_VALUE_NUMBER, {.number = 3.1f}};
extern void topic_houqi_liftruninfo_post(void);

enum {
    RT_ACCEL,
    RT_SPEED,
    RT_DISTANCE,
    RT_DIRECTION,
    RT_FLOOR,
    RT_JITTER_FREQ,
    RT_JITTER_ACCEL,
    __RT_MAX
};

static const struct blobmsg_policy realtime_policy[__RT_MAX] = {
    [RT_ACCEL] = {.name = "accel", .type = BLOBMSG_TYPE_DOUBLE},
    [RT_SPEED] = {.name = "velocity", .type = BLOBMSG_TYPE_DOUBLE},
    [RT_DISTANCE] = {.name = "distance", .type = BLOBMSG_TYPE_DOUBLE},
    [RT_DIRECTION] = {.name = "direction", .type = BLOBMSG_TYPE_INT32},
    [RT_FLOOR] = {.name = "floor", .type = BLOBMSG_TYPE_INT32},
    [RT_JITTER_FREQ] = {.name = "jitter_freq", .type = BLOBMSG_TYPE_DOUBLE},
    [RT_JITTER_ACCEL] = {.name = "jitter_accel", .type = BLOBMSG_TYPE_DOUBLE},
};

enum {
    HI_DISTANCE,
    HI_DIRECTION,
    HI_TIMESTAMP_BEGIN,
    HI_TIMESTAMP_END,
    HI_FLOOR_BEGIN,
    HI_FLOOR_END,
    HI_ACCEL_ARRAY,
    HI_SPEED_ARRAY,
    HI_JITTER_FREQ_ARRAY,
    HI_JITTER_ACCEL_ARRAY,
    __HI_MAX
};

static const struct blobmsg_policy historical_policy[__HI_MAX] = {
    [HI_DISTANCE] = {.name = "distance", .type = BLOBMSG_TYPE_DOUBLE},
    [HI_DIRECTION] = {.name = "direction", .type = BLOBMSG_TYPE_INT32},
    [HI_TIMESTAMP_BEGIN] = {.name = "timestamp_begin", .type = BLOBMSG_TYPE_INT64},
    [HI_TIMESTAMP_END] = {.name = "timestamp_end", .type = BLOBMSG_TYPE_INT64},
    [HI_FLOOR_BEGIN] = {.name = "floor_begin", .type = BLOBMSG_TYPE_INT32},
    [HI_FLOOR_END] = {.name = "floor_end", .type = BLOBMSG_TYPE_INT32},
    [HI_ACCEL_ARRAY] = {.name = "accels", .type = BLOBMSG_TYPE_ARRAY},
    [HI_SPEED_ARRAY] = {.name = "speeds", .type = BLOBMSG_TYPE_ARRAY},
    [HI_JITTER_FREQ_ARRAY] = {.name = "jitter_freqs", .type = BLOBMSG_TYPE_ARRAY},
    [HI_JITTER_ACCEL_ARRAY] = {.name = "jitter_accels", .type = BLOBMSG_TYPE_ARRAY},
};

enum {
    M_STATE,
    __M_MAX
};

static const struct blobmsg_policy motion_policy[__M_MAX] = {
    [M_STATE] = {.name = "state", .type = BLOBMSG_TYPE_INT32},
};

static int elevatord_subscriber_callback(struct ubus_context* ctx, struct ubus_object* obj, struct ubus_request_data* req, const char* method, struct blob_attr* msg) {
    (void)ctx;
    (void)obj;
    (void)req;
    (void)method;

    // char* str;

    if (!method) {
        return -1;
    }

    // str = blobmsg_format_json(msg, true);
    // HR_LOGE("%s(%d): receive ....\n", __FUNCTION__, __LINE__);
    // HR_LOGE("%s(%d): str:%s\n", __FUNCTION__, __LINE__, str);
    // printf("{ \"%s\": %s }\n", method, str);
    // free(str);

    if (0 == strcmp(ELEVATORD_EVENT_REALTIME, method)) {
        // 实时数据处理
        struct blob_attr* tb[__RT_MAX] = {NULL};
        blobmsg_parse(realtime_policy, __RT_MAX, tb, blobmsg_data(msg),
                      blobmsg_data_len(msg));

        if (tb[RT_ACCEL])
            _status.accel = blobmsg_get_double(tb[RT_ACCEL]);
        if (tb[RT_SPEED])
            _status.speed = fabs(blobmsg_get_double(tb[RT_SPEED]));
        if (tb[RT_DISTANCE])
            _status.distance = blobmsg_get_double(tb[RT_DISTANCE]);
        if (tb[RT_DIRECTION]) {
            _status.direction = blobmsg_get_u32(tb[RT_DIRECTION]);
        }
        // cast from uint32_t
        if (tb[RT_FLOOR])
            _status.current_floor = (int)blobmsg_get_u32(tb[RT_FLOOR]);

        if (tb[RT_JITTER_FREQ])
            _status.jitter_freq = blobmsg_get_double(tb[RT_JITTER_FREQ]);
        if (tb[RT_JITTER_ACCEL])
            _status.jitter_accel = blobmsg_get_double(tb[RT_JITTER_ACCEL]);

        if (_status.speed > _speed_limit_threhold.value.number) {
            if (0 == (_elevator_exception & ELEVATOR_EXCEPTION_OVERSPEED)) {
                _elevator_exception |= ELEVATOR_EXCEPTION_OVERSPEED;
                elevator_fault_occurred(ELEVATOR_EXCEPTION_OVERSPEED);
                HR_LOGD("%s(%d): speed too high %f > %f .............\n", __FUNCTION__, __LINE__, _status.speed, _speed_limit_threhold.value.number);
                printf("%s(%d): speed too high %f > %f .............\n", __FUNCTION__, __LINE__, _status.speed, _speed_limit_threhold.value.number);
            }
        } else {
            if (0 != (_elevator_exception & ELEVATOR_EXCEPTION_OVERSPEED)) {
                _elevator_exception &= ~ELEVATOR_EXCEPTION_OVERSPEED;
                elevator_fault_resolved(ELEVATOR_EXCEPTION_OVERSPEED);
                HR_LOGD("%s(%d): !!! speed resume .............\n", __FUNCTION__, __LINE__);
                printf("%s(%d): !!! speed resume .............\n", __FUNCTION__, __LINE__);
            }
        }

        printf("%s(%d): realtime: accel:%f, speed:%f, distance:%f, direction:%d, floor:%d\n", __FUNCTION__, __LINE__,
        _status.accel, _status.speed, _status.distance, _status.direction, _status.current_floor);

    } else if (0 == strcmp(ELEVATORD_EVENT_MOTION, method)) {
        struct blob_attr* tb[__M_MAX] = {NULL};
        blobmsg_parse(motion_policy, __M_MAX, tb, blobmsg_data(msg),
                      blobmsg_data_len(msg));

        if (!tb[M_STATE]) {
            return -1;
        }

        int state = blobmsg_get_u32(tb[M_STATE]);
        if (state == 0) {
            statemachine_post(SM_EVENT_STOPPED);
        } else {
            statemachine_post(SM_EVENT_RUNNING);
        }

    } else if (0 == strcmp(ELEVATORD_EVENT_HISTORICAL, method)) {
        // directly pass for LiftRunInfo
        struct blob_attr* cur = NULL;
        size_t rem;
        struct blob_attr* tb[__HI_MAX] = {NULL};
        blobmsg_parse(historical_policy, __HI_MAX, tb, blobmsg_data(msg),
                      blobmsg_data_len(msg));

        if (!tb[HI_DISTANCE] || !tb[HI_DIRECTION] ||
            !tb[HI_TIMESTAMP_BEGIN] || !tb[HI_TIMESTAMP_END] ||
            !tb[HI_FLOOR_BEGIN] || !tb[HI_FLOOR_END] ||
            !tb[HI_ACCEL_ARRAY] || !tb[HI_SPEED_ARRAY] ||
            !tb[HI_JITTER_FREQ_ARRAY] || !tb[HI_JITTER_ACCEL_ARRAY]) {
            return 0;
        }

        _historical.distance = blobmsg_get_double(tb[HI_DISTANCE]);
        _historical.direction = (int)blobmsg_get_u32(tb[HI_DIRECTION]);
        _historical.timestamp_begin = blobmsg_get_u64(tb[HI_TIMESTAMP_BEGIN]);
        _historical.timestamp_end = blobmsg_get_u64(tb[HI_TIMESTAMP_END]);
        _historical.floor_begin = (int)blobmsg_get_u32(tb[HI_FLOOR_BEGIN]);
        _historical.floor_end = (int)blobmsg_get_u32(tb[HI_FLOOR_END]);

        HR_LOGD("historical: distance:%f, direction:%d, timestamp:%lu -> %lu(%lu), floor: %d -> %d\n", _historical.distance, _historical.direction,
                _historical.timestamp_begin, _historical.timestamp_end, _historical.timestamp_end - _historical.timestamp_begin,
                _historical.floor_begin, _historical.floor_end);

        hrbuffer_reset(&_historical.accel_array);
        hrbuffer_reset(&_historical.speed_array);
        hrbuffer_reset(&_historical.jitter_frequency_array);
        hrbuffer_reset(&_historical.jitter_accel_array);

        if (fabs(_historical.distance) < 0.5) {
            HR_LOGE("distance:%f is too short! maybe invalid data\n", _historical.distance);
            return 0;
        }

        blobmsg_for_each_attr(cur, tb[HI_ACCEL_ARRAY], rem) {
            double v = blobmsg_get_double(cur);
            hrbuffer_append(&_historical.accel_array, &v, sizeof(v));
        }
        blobmsg_for_each_attr(cur, tb[HI_SPEED_ARRAY], rem) {
            double v = blobmsg_get_double(cur);
            hrbuffer_append(&_historical.speed_array, &v, sizeof(v));
        }
        blobmsg_for_each_attr(cur, tb[HI_JITTER_FREQ_ARRAY], rem) {
            double v = blobmsg_get_double(cur);
            hrbuffer_append(&_historical.jitter_frequency_array, &v, sizeof(v));
        }
        blobmsg_for_each_attr(cur, tb[HI_JITTER_ACCEL_ARRAY], rem) {
            double v = blobmsg_get_double(cur);
            hrbuffer_append(&_historical.jitter_accel_array, &v, sizeof(v));
        }

        topic_houqi_liftruninfo_post();
    }

    return 0;
}

static int subscriber_elevatord_event() {
    if (_elevatord_object_id != 0) {
        // have subscribed
        return 0;
    }
    if (0 == ubus_lookup_id(_ubus_ctx, ELEVATORD_NAME, &_elevatord_object_id)) {
        if (0 == ubus_subscribe(_ubus_ctx, &_elevatord_subscriber, _elevatord_object_id)) {
            return 0;
        }
    }

    // reset id when failed
    _elevatord_object_id = 0;

    return -1;
}

enum {
    OE_ID,
    OE_PATH,
    __OE_MAX
};

static const struct blobmsg_policy object_event_policy[__OE_MAX] = {
    [OE_ID] = {.name = "id", .type = BLOBMSG_TYPE_INT32},
    [OE_PATH] = {.name = "path", .type = BLOBMSG_TYPE_STRING},
};

static void ubus_event_handler(struct ubus_context* ctx,
                               struct ubus_event_handler* ev,
                               const char* type,
                               struct blob_attr* msg) {
    (void)ev;

    struct blob_attr* tb[__OE_MAX] = {NULL};
    char* str = blobmsg_format_json(msg, true);
    HR_LOGD("%s(%d) %s: %s\n", __FUNCTION__, __LINE__, type, str);
    free(str);

    if (strcmp(type, "ubus.object.add") == 0) {
        blobmsg_parse(object_event_policy, __OE_MAX, tb, blobmsg_data(msg),
                      blobmsg_data_len(msg));

        if (!tb[OE_ID] || !tb[OE_PATH]) {
            HR_LOGE("can not parse event\n");
            return;
        }

        if (0 == strcmp(ELEVATORD_NAME, blobmsg_data(tb[OE_PATH]))) {
            HR_LOGD("elevatord connected, subcribe it!\n");
            subscriber_elevatord_event();
        }

    } else if (strcmp(type, "ubus.object.remove") == 0) {
        blobmsg_parse(object_event_policy, __OE_MAX, tb, blobmsg_data(msg),
                      blobmsg_data_len(msg));

        if (!tb[OE_ID] || !tb[OE_PATH]) {
            HR_LOGE("can not parse event\n");
            return;
        }

        if (0 == strcmp(ELEVATORD_NAME, blobmsg_data(tb[OE_PATH]))) {
            uint32_t id = _elevatord_object_id;
            _elevatord_object_id = 0;
            HR_LOGD("elevatord disconnected, unsubcribe it!\n");
            if (id != 0) {
                ubus_unsubscribe(ctx, &_elevatord_subscriber, id);
            }
        }
    } else if (strncmp(type, ELEVATOR_EVENT_PREFIX, strlen(ELEVATOR_EVENT_PREFIX)) == 0) {
        const char* event = type + strlen(ELEVATOR_EVENT_PREFIX);
        // HR_LOGD("%s(%d): type:%s -> %s\n", __FUNCTION__, __LINE__, type, event);
        // door
        // person
        // ebike

        if (0 == strcmp("door", event)) {
            static const struct blobmsg_policy policy[] = {
                {.name = "status", .type = BLOBMSG_TYPE_STRING},
                {NULL, BLOBMSG_TYPE_UNSPEC},
            };

            blobmsg_parse(policy, sizeof(policy) / sizeof(policy[0]), tb, blobmsg_data(msg),
                          blobmsg_data_len(msg));

            if (!tb[0]) {
                return;
            }

            const char* status = blobmsg_get_string(tb[0]);
            if (!status) {
                return;
            }
            if (0 == strcmp("open", status)) {
                _status.door_state = ELEVATOR_DOOR_OPEN;
                statemachine_post(SM_EVENT_DOOR_OPENED);
            } else if (0 == strcmp("close", status)) {
                _status.door_state = ELEVATOR_DOOR_CLOSE;
                statemachine_post(SM_EVENT_DOOR_CLOSED);
            }
        } else if (0 == strcmp("person", event)) {
            int num = 0;
            static const struct blobmsg_policy policy[] = {
                {.name = "num", .type = BLOBMSG_TYPE_INT32},
                {NULL, BLOBMSG_TYPE_UNSPEC},
            };

            blobmsg_parse(policy, sizeof(policy) / sizeof(policy[0]), tb, blobmsg_data(msg),
                          blobmsg_data_len(msg));

            if (!tb[0]) {
                return;
            }

            num = blobmsg_get_u32(tb[0]);
            _status.passenger_count = num;
        } else if (0 == strcmp("ebike", event)) {
            static const struct blobmsg_policy policy[] = {
                {.name = "status", .type = BLOBMSG_TYPE_INT32},
                {NULL, BLOBMSG_TYPE_UNSPEC},
            };

            blobmsg_parse(policy, sizeof(policy) / sizeof(policy[0]), tb, blobmsg_data(msg),
                          blobmsg_data_len(msg));

            if (!tb[0]) {
                return;
            }

            if (blobmsg_get_u32(tb[0]) == 1) {
                HR_LOGD("receive ebike fire event!\n");
            } else {
                HR_LOGD("receive ebike cancel event!\n");
            }
        }
    }
}

static struct ubus_event_handler _ubus_event = {
    .cb = ubus_event_handler,
};

static void _reconnect_timer(struct uloop_timeout* timeout) {
    (void)timeout;
    int t = _UBUS_RETRY_TIMEOUT;

    static struct uloop_timeout retry = {
        .cb = _reconnect_timer,
    };

    if (!_ubus_ctx)
        return;

    if (ubus_reconnect(_ubus_ctx, UBUS_SOCK) != 0) {
        printf("failed to reconnect, trying again in %d seconds\n", t);
        uloop_timeout_set(&retry, t * 1000);
        return;
    }

    printf("reconnected to ubus, new id: %08x\n", _ubus_ctx->local_id);

#if 1  // we should re subscriber event?

    _elevatord_object_id = 0;
    ubus_register_subscriber(_ubus_ctx, &_elevatord_subscriber);

    ubus_register_event_handler(_ubus_ctx, &_ubus_event, "ubus.object.*");
    ubus_register_event_handler(_ubus_ctx, &_ubus_event, ELEVATOR_EVENT_PREFIX "*");

    subscriber_elevatord_event();

#endif

    ubus_add_uloop(_ubus_ctx);

#ifdef FD_CLOEXEC
    fcntl(_ubus_ctx->sock.fd, F_SETFD, fcntl(_ubus_ctx->sock.fd, F_GETFD) | FD_CLOEXEC);
#endif
}

static void _connection_lost(struct ubus_context* ctx) {
    (void)ctx;
    _reconnect_timer(NULL);
}

static void _pipe_uloop_main_thread_handler(struct uloop_fd* u, unsigned int events) {
    (void)u;
    (void)events;
    int which = -1;
    read(_pipefd[0], &which, sizeof(which));

    HR_LOGD("receive message:%d \n", which);
    switch (which) {
        case MSG_QUIT:
            HR_LOGD("receive message:%d quit\n", which);
            uloop_end();
            break;
    }
}

static void post_message(int which) {
    write(_pipefd[1], &which, sizeof(which));
}
static void* uelevator_thread_routin(void* args) {
    (void)args;
    // int rc = -1;

    //  adjust output line buffered mode
    setvbuf(stdout, NULL, _IOLBF, 0);

    struct uloop_fd pipe_fd = {
        .fd = _pipefd[0],
        .cb = _pipe_uloop_main_thread_handler,
    };

    uloop_init();

    while (_request_exit != 1) {
        _ubus_ctx = ubus_connect(UBUS_SOCK);
        if (_ubus_ctx) {
            break;
        }
        HR_LOGD("%s(%d): can not connect\n", __FUNCTION__, __LINE__);
        usleep(1000 * 1000);
        // no need call testcancel because usleep is cancel point
        pthread_testcancel();
    }

    _ubus_ctx->connection_lost = _connection_lost;

    ubus_add_uloop(_ubus_ctx);

#ifdef FD_CLOEXEC
    fcntl(_ubus_ctx->sock.fd, F_SETFD, fcntl(_ubus_ctx->sock.fd, F_GETFD) | FD_CLOEXEC);
#endif

    memset(&_elevatord_subscriber, 0, sizeof(_elevatord_subscriber));
    _elevatord_subscriber.cb = elevatord_subscriber_callback;

    ubus_register_subscriber(_ubus_ctx, &_elevatord_subscriber);

    ubus_register_event_handler(_ubus_ctx, &_ubus_event, "ubus.object.*");
    ubus_register_event_handler(_ubus_ctx, &_ubus_event, "elevator.event.*");

    subscriber_elevatord_event();

    uloop_fd_add(&pipe_fd, ULOOP_READ);

    uloop_run();

    ubus_unregister_event_handler(_ubus_ctx, &_ubus_event);
    ubus_unregister_subscriber(_ubus_ctx, &_elevatord_subscriber);
    ubus_free(_ubus_ctx);
    _ubus_ctx = NULL;

    uloop_done();

    return NULL;
}
int uelevator_init(void) {
    int ret = -1;
    pthread_attr_t attr;

    if (_uobject_tid != 0) {
        return -1;
    }

    _status.door_state = ELEVATOR_DOOR_CLOSE;

    blob_buf_init(&_b, 0);
    blob_buf_grow(&_b, 1024);

    hrbuffer_alloc(&_historical.accel_array, sizeof(double) * 5 * 60);
    hrbuffer_alloc(&_historical.speed_array, sizeof(double) * 5 * 60);
    hrbuffer_alloc(&_historical.jitter_frequency_array, sizeof(double) * 5 * 60);
    hrbuffer_alloc(&_historical.jitter_accel_array, sizeof(double) * 5 * 60);

    if (pipe(_pipefd) < 0) {
        perror("pipe");
        return -1;
    }

    sconf_load_with_proto(HQLIFTD_CONFIG_PATH, &_speed_limit_threhold, 1);

    pthread_attr_init(&attr);

    ret = pthread_create(&_uobject_tid, &attr, uelevator_thread_routin, NULL);
    if (0 != ret) {
        HR_LOGE("%s(%d): failed to pthread_create\n", __FUNCTION__, __LINE__);
        return -1;
    }
    pthread_attr_destroy(&attr);

    return 0;
}

struct ubus_context* uelevator_get_ubus_ctx() {
    return _ubus_ctx;
}
int uelevator_deinit(void) {
    if (_uobject_tid != 0) {
        HR_LOGD("uobject send exit ...\n");
        post_message(MSG_QUIT);

        _request_exit = 1;
        // usleep(100);
        // do not use cancel, we request thread to exit normal
        // otherwise release maybe not release
        // pthread_cancel(_uobject_tid);
        pthread_join(_uobject_tid, NULL);
        HR_LOGD("uobject exit ...\n");
#if 0
        if (_ubus_ctx) {
            HR_LOGD("uobject exit clean...\n");
            ubus_unregister_event_handler(_ubus_ctx, &_object_event);
            ubus_unregister_subscriber(_ubus_ctx, &_elevatord_subscriber);
            ubus_free(_ubus_ctx);
            _ubus_ctx = NULL;
        }
        HR_LOGD("uobject exit2 ...\n");
#endif
        _uobject_tid = 0;
    }

    blob_buf_free(&_b);

    hrbuffer_free(&_historical.accel_array);
    hrbuffer_free(&_historical.speed_array);
    hrbuffer_free(&_historical.jitter_frequency_array);
    hrbuffer_free(&_historical.jitter_accel_array);

    return 0;
}

int uelevator_get_status(struct elevator_status* st) {
    if (!st)
        return -1;

    memcpy(st, &_status, sizeof(struct elevator_status));
    return 0;
}

int uelevator_get_historical(struct elevator_historical** h) {
    if (!h) {
        return -1;
    }

    *h = &_historical;
    return 0;
}
