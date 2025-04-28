#include <pthread.h>
#include <stdint.h>
#include <stdio.h>

#include <math.h>
#include "hr_buffer.h"
#include "hr_log.h"
#include "libubox/blob.h"
#include "libubox/blobmsg.h"
#include "libubox/blobmsg_json.h"
#include "libubus.h"
#include "motion.h"
#include "time_utils.h"

#define UBUS_SOCK "/tmp/ubus.sock"

#define _UBUS_RETRY_TIMEOUT (2)

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

static struct ubus_context* _ubus_ctx = NULL;

static pthread_t _uobject_tid = 0;

extern struct ubus_object _elevatord_object;

static int64_t _now = 0;
static struct blob_buf _b;
static struct hrbuffer _velocity_buffer;
// static struct blob_buf _velocity_array;
//  static void* _velocity_array_handle;
//  static struct blob_buf _accel_array;

static enum motion_state _running_state = STOPPED;
static enum motion_direction _running_direction = DIRECTION_NONE;

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
    fcntl(g_ubus_ctx->sock.fd, F_SETFD, fcntl(g_ubus_ctx->sock.fd, F_GETFD) | FD_CLOEXEC);
#endif
}

static void _connection_lost(struct ubus_context* ctx) {
    (void)ctx;
    _reconnect_timer(NULL);
}

void* uobject_elevator_thread_routin(void* args) {
    (void)args;
    int rc = -1;
    // adjust output line buffered mode
    setvbuf(stdout, NULL, _IOLBF, 0);

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

    uloop_run();

    uloop_done();

    return NULL;
}
int uobject_elevatord_init(void) {
    int ret = -1;
    pthread_attr_t attr;

    if (_uobject_tid != 0) {
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
    hrbuffer_alloc(&_velocity_buffer, 1024 * sizeof(double));  // 1s -> 5 elements
    motion_register_observer(&_ubus_observer);
    return 0;
}

struct ubus_context* uelevatord_get_ubus_ctx() {
    return _ubus_ctx;
}
int uobject_elevatord_deinit(void) {
    if (_uobject_tid != 0) {
        pthread_cancel(_uobject_tid);
        pthread_join(_uobject_tid, NULL);
        HR_LOGD("uobject exit ...\n");
        _uobject_tid = 0;
    }
    return 0;
}

static void _observer_on_status(struct motion_status* st) {
    if (!st)
        return;
    // HR_LOGD("speed : %f\n", _speed_realtime);

    // ms
    int64_t now = get_monotonic_nanoseconds() / 1000000;

    if (now - _now < 200) {
        return;
    }
    // also we can simple using sampling_rate

    HR_LOGD("capture data for hq\n");
    // if (!_velocity_array_handle) {
    //      _velocity_array_handle = blobmsg_open_array(&_b, "acceleration");
    // }

    // blobmsg_add_double(&_velocity_array, NULL, st->accel);

    st->accel = round(st->accel * 100) / 100;
    HR_LOGD("acc:%f\n", st->accel);
    hrbuffer_append(&_velocity_buffer, &st->accel, sizeof(st->accel));
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
        HR_LOGD("stopped --> running, direction:%d, distance:%f\n", data->direction, data->distance);
    } else if (data->state == STOPPED) {
        HR_LOGD("running --> stopped, direction:%d, distance:%f\n", data->direction, data->distance);
        blob_buf_init(&_b, 0);

        void* root = blobmsg_open_array(&_b, "acceleration");

        for (size_t i = 0; i < _velocity_buffer.offset;) {
            double v = *((double*)(_velocity_buffer.data + i));
    HR_LOGD("acc2v..........mxp :%f, offset:%d\n", v, _velocity_buffer.offset);
            blobmsg_add_double(&_b, NULL, v);
            i += sizeof(double);
        }
        blobmsg_close_array(&_b, root);
        char* str = blobmsg_format_json(_b.head, true);
        HR_LOGD("%s\n", str);
        free(str);
    }

    _running_state = data->state;
}
