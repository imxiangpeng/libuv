#include <pthread.h>
#include <stdint.h>
#include <stdio.h>

#include <math.h>
#include "hr_buffer.h"
#include "hr_log.h"
#include "libubox/blob.h"
#include "libubox/blobmsg.h"
#include "libubox/blobmsg_json.h"
#include "libubox/uloop.h"
#include "libubus.h"
#include "motion.h"
#include "time_utils.h"

#define UBUS_SOCK "/tmp/ubus.sock"

#define DATA_SAMPLE_INTERVAL_MS 200  // houqi data array elements interval 200ms
#define DATA_SAMPLE_SIZE_MAX 7200    // limit 3min, too much data: 8*5*60*3
#define ELEVATORD_EVENT_REALTIME "RealTime"
#define ELEVATORD_EVENT_HISTORICAL "Historical"
#define ELEVATORD_EVENT_MOTION "Motion"

#define _UBUS_RETRY_TIMEOUT (2)

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

enum {
    MSG_REALTIME,
    MSG_HISTORICAL,
    MSG_MOTION_EVENT,
    MSG_QUIT
};
static struct ubus_context* _ubus_ctx = NULL;

struct uloop_timeout _loop_thread_notify_timer;

static int _pipefd[2];  // [0]=read, [1]=write
static pthread_t _uobject_tid = 0;

extern struct ubus_object _elevatord_object;

static int64_t _now = 0;
static struct blob_buf _b;
static int _b_is_busy = 0;
static struct blob_buf _realtime_b;
static int _realtime_b_is_busy = 0;
static struct blob_buf _motion_b;
static struct hrbuffer _accel_buffer;
static struct hrbuffer _velocity_buffer;
static struct hrbuffer _jitter_accel_buffer;
static struct hrbuffer _jitter_freq_buffer;
// static struct blob_buf _velocity_array;
//  static void* _velocity_array_handle;
//  static struct blob_buf _accel_array;

// 500ms or 1s report to ubus client
static int _realtime_report_times = 0;
static const int _realtime_report_fac = 100;  // 10 * sampling_rate = 100 * 1/100 = 1s

static enum motion_state _running_state = STOPPED;
static enum motion_direction _running_direction = DIRECTION_NONE;
static int _begin_floor = 0;

static void _observer_on_status(struct motion_status* st);
static void _observer_on_event(struct motion_event* data);

static struct motion_observer _ubus_observer = {
    .on_status = _observer_on_status,
    .on_event = _observer_on_event,
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
    ubus_add_uloop(_ubus_ctx);

#ifdef FD_CLOEXEC
    fcntl(_ubus_ctx->sock.fd, F_SETFD, fcntl(g_ubus_ctx->sock.fd, F_GETFD) | FD_CLOEXEC);
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

    HR_LOGD("haha receive message:%d \n", which);
    switch (which) {
        case MSG_REALTIME:
            HR_LOGD("haha report realtime message \n");
            ubus_notify(_ubus_ctx, &_elevatord_object, ELEVATORD_EVENT_REALTIME, _realtime_b.head, -1 /*no block*/);
            _realtime_b_is_busy = 0;
            break;
        case MSG_HISTORICAL:
            HR_LOGD("haha receive notify message \n");
            ubus_notify(_ubus_ctx, &_elevatord_object, ELEVATORD_EVENT_HISTORICAL, _b.head, -1 /*no block*/);
            _b_is_busy = 0;
            break;
        case MSG_MOTION_EVENT:
            HR_LOGD("haha receive motion event message \n");
            ubus_notify(_ubus_ctx, &_elevatord_object, ELEVATORD_EVENT_MOTION, _motion_b.head, -1 /*no block*/);
            break;
        case MSG_QUIT:
            HR_LOGD("haha receive message:%d quit\n", which);
            uloop_end();
            break;
    }
}

static void post_message(int which) {
    write(_pipefd[1], &which, sizeof(which));
}
void* uobject_elevator_thread_routin(void* args) {
    (void)args;
    int rc = -1;
    // adjust output line buffered mode
    setvbuf(stdout, NULL, _IOLBF, 0);

    struct uloop_fd pipe_fd = {
        .fd = _pipefd[0],
        .cb = _pipe_uloop_main_thread_handler,
    };

    uloop_init();

    _ubus_ctx = ubus_connect(UBUS_SOCK);
    if (!_ubus_ctx) {
        HR_LOGD("%s(%d): can not connect\n", __FUNCTION__, __LINE__);
        return NULL;
    }

    _ubus_ctx->connection_lost = _connection_lost;

    ubus_add_uloop(_ubus_ctx);

#ifdef FD_CLOEXEC
    fcntl(_ubus_ctx->sock.fd, F_SETFD, fcntl(g_ubus_ctx->sock.fd, F_GETFD) | FD_CLOEXEC);
#endif

    rc = ubus_add_object(_ubus_ctx, &_elevatord_object);
    if (0 != rc) {
        HR_LOGE("can not add object %s -> %s\n", _elevatord_object.name, ubus_strerror(rc));
    }

    uloop_fd_add(&pipe_fd, ULOOP_READ);
    uloop_run();

    ubus_free(_ubus_ctx);
    _ubus_ctx = NULL;
    uloop_done();

    return NULL;
}
int uobject_elevatord_init(void) {
    int ret = -1;
    pthread_attr_t attr;

    if (_uobject_tid != 0) {
        return -1;
    }

    if (pipe(_pipefd) < 0) {
        perror("pipe");
        return -1;
    }

    pthread_attr_init(&attr);

    ret = pthread_create(&_uobject_tid, &attr, uobject_elevator_thread_routin, NULL);
    if (0 != ret) {
        HR_LOGE("%s(%d): failed to pthread_create\n", __FUNCTION__, __LINE__);
        return -1;
    }
    pthread_attr_destroy(&attr);

    blob_buf_init(&_b, 0);
    blob_buf_grow(&_b, 4096);
    // blob_buf_init(&_velocity_array, 0);
    // blob_buf_grow(&_b, 4096);
    hrbuffer_alloc(&_accel_buffer, 1024 * sizeof(double));         // 1s -> 5 elements
    hrbuffer_alloc(&_velocity_buffer, 1024 * sizeof(double));      // 1s -> 5 elements
    hrbuffer_alloc(&_jitter_freq_buffer, 1024 * sizeof(double));   // 1s -> 5 elements
    hrbuffer_alloc(&_jitter_accel_buffer, 1024 * sizeof(double));  // 1s -> 5 elements
    motion_register_observer(&_ubus_observer);
    return 0;
}

struct ubus_context* uelevatord_get_ubus_ctx() {
    return _ubus_ctx;
}
int uobject_elevatord_deinit(void) {
    motion_unregister_observer(&_ubus_observer);
    if (_uobject_tid != 0) {
        post_message(MSG_QUIT);
        // pthread_cancel(_uobject_tid);
        pthread_join(_uobject_tid, NULL);
        HR_LOGD("uobject exit ...\n");
        _uobject_tid = 0;

        blob_buf_free(&_b);
        blob_buf_free(&_realtime_b);

        hrbuffer_free(&_accel_buffer);
        hrbuffer_free(&_velocity_buffer);
        hrbuffer_free(&_jitter_freq_buffer);
        hrbuffer_free(&_jitter_accel_buffer);
    }
    return 0;
}

static void _observer_on_status(struct motion_status* st) {
    double accel, velocity, jitter_accel, jitter_frequency;
    if (!st)
        return;
    // HR_LOGD("speed : %f\n", _speed_realtime);

    HR_LOGD("_report times:%d\n", _realtime_report_times);
    if (_realtime_report_times % _realtime_report_fac == 0) {
        if (!_realtime_b_is_busy) {
            _realtime_b_is_busy = 1;
            blob_buf_init(&_realtime_b, 0);
            // blob_put_raw(struct blob_buf *buf, const void *ptr, unsigned int len)
            // obmsg_add_double
            //  blobmsg_add_u8();
            blobmsg_add_double(&_realtime_b, "accel", st->accel);
            blobmsg_add_double(&_realtime_b, "velocity", st->velocity);
            blobmsg_add_double(&_realtime_b, "distance", st->distance);
            blobmsg_add_u32(&_realtime_b, "direction", _running_direction);
            blobmsg_add_u32(&_realtime_b, "floor", (uint32_t)st->floor);

            post_message(MSG_REALTIME);
        } else {
            HR_LOGE("drop .........\n");
            // drop this time trigger next time
            _realtime_report_times--;
        }
    }

    _realtime_report_times++;

    if (_running_state == STOPPED) {
        return;
    }
    // ms
    int64_t now = get_monotonic_nanoseconds() / 1000000;

    // report every 200ms
    if (now - _now < DATA_SAMPLE_INTERVAL_MS) {
        return;
    }

    _now = now;
    // also we can simple using sampling_rate

    // if (!_velocity_array_handle) {
    //      _velocity_array_handle = blobmsg_open_array(&_b, "acceleration");
    // }

    // blobmsg_add_double(&_velocity_array, NULL, st->accel);

    accel = round(st->accel * 100) / 100;
    velocity = fabs(round(st->velocity * 100) / 100);
    jitter_accel = round(st->jitter_accel * 100) / 100;
    jitter_frequency = round(st->jitter_frequency * 100) / 100;
    // double *v = (double*)(_velocity_buffer.data + _velocity_buffer.offset);

    if (_accel_buffer.offset >= DATA_SAMPLE_SIZE_MAX) {
        // drop data
        return;
    }

    hrbuffer_append(&_accel_buffer, &accel, sizeof(accel));
    hrbuffer_append(&_velocity_buffer, &velocity, sizeof(velocity));
    hrbuffer_append(&_jitter_freq_buffer, &jitter_frequency, sizeof(jitter_frequency));
    hrbuffer_append(&_jitter_accel_buffer, &jitter_accel, sizeof(jitter_accel));
    // HR_LOGD("acc:%f, prev:%f, %p\n", st->accel, *v, v);
    //     blobmsg_add_field(&b, BLOBMSG_TYPE_ARRAY, "array1", arr1.head, blob_raw_len(arr1.head));
}
static void _observer_on_event(struct motion_event* data) {
    if (!data)
        return;

    if (_running_direction != data->direction) {
        HR_LOGD("direction changed ......\n");
    }

    _running_direction = data->direction;
    if (data->state == _running_state) {
        //    return;
        HR_LOGD("state not changed ......\n");
    }

    // begin running
    if (_running_state == STOPPED) {
        _begin_floor = data->floor;

        blob_buf_init(&_motion_b, 0);
        blobmsg_add_u32(&_motion_b, "state", data->state);
        post_message(MSG_MOTION_EVENT);

        HR_LOGD("stopped --> running, direction:%d, distance:%f\n", data->direction, data->distance);
    } else if (data->state == STOPPED) {
        HR_LOGD("running --> stopped, direction:%d, distance:%f\n", data->direction, data->distance);

        if (!_b_is_busy) {
            HR_LOGE("_b is busy maybe we should drop or wait.........\n");

            blob_buf_init(&_b, 0);

            blobmsg_add_double(&_b, "distance", fabs(data->distance));
            blobmsg_add_u32(&_b, "direction", data->direction);
            blobmsg_add_u64(&_b, "beginTimeStamp", data->timestamp_begin);
            blobmsg_add_u64(&_b, "endTimeStamp", data->timestamp_end);
            blobmsg_add_u64(&_b, "beginFloor", data->floor_begin);
            blobmsg_add_u64(&_b, "endFloor", data->floor);

            void* root = blobmsg_open_array(&_b, "acceleration");

            for (size_t i = 0; i < _accel_buffer.offset;) {
                double* v = (double*)(_accel_buffer.data + i);
                blobmsg_add_double(&_b, NULL, *v);
                i += sizeof(double);
            }
            blobmsg_close_array(&_b, root);
            root = blobmsg_open_array(&_b, "runSpeed");

            for (size_t i = 0; i < _velocity_buffer.offset;) {
                double* v = (double*)(_velocity_buffer.data + i);
                blobmsg_add_double(&_b, NULL, *v);
                i += sizeof(double);
            }
            blobmsg_close_array(&_b, root);

            root = blobmsg_open_array(&_b, "jitterFrequency");

            for (size_t i = 0; i < _jitter_freq_buffer.offset;) {
                double* v = (double*)(_jitter_freq_buffer.data + i);
                blobmsg_add_double(&_b, NULL, *v);
                i += sizeof(double);
            }
            blobmsg_close_array(&_b, root);
            root = blobmsg_open_array(&_b, "jitterAcceleration");

            for (size_t i = 0; i < _jitter_accel_buffer.offset;) {
                double* v = (double*)(_jitter_accel_buffer.data + i);
                blobmsg_add_double(&_b, NULL, *v);
                i += sizeof(double);
            }
            blobmsg_close_array(&_b, root);

            char* str = blobmsg_format_json(_b.head, true);
            HR_LOGD("%s\n", str);
            free(str);

            post_message(MSG_HISTORICAL);

            hrbuffer_reset(&_accel_buffer);
            hrbuffer_reset(&_velocity_buffer);
            hrbuffer_reset(&_jitter_accel_buffer);
            hrbuffer_reset(&_jitter_freq_buffer);
        }
    }

    _running_state = data->state;
}
