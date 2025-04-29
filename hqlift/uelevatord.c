#include <pthread.h>
#include <stdint.h>
#include <stdio.h>

#include <math.h>
#include "hr_log.h"
#include "libubox/blob.h"
#include "libubox/blobmsg.h"
#include "libubox/blobmsg_json.h"
#include "libubus.h"
#include "time_utils.h"

#define UBUS_SOCK "/tmp/ubus.sock"

#define _UBUS_RETRY_TIMEOUT (2)

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

static struct ubus_context* _ubus_ctx = NULL;

static pthread_t _uobject_tid = 0;

// static int64_t _now = 0;
static struct blob_buf _b;

// static struct blob_buf _velocity_array;
//  static void* _velocity_array_handle;
//  static struct blob_buf _accel_array;
static int elevatord_subscriber_callback(struct ubus_context* ctx, struct ubus_object* obj, struct ubus_request_data* req, const char* method, struct blob_attr* msg) {
    (void)ctx;
    (void)obj;
    (void)req;
    (void)method;
    char* str;

    str = blobmsg_format_json(msg, true);
    HR_LOGE("%s(%d): receive ....\n", __FUNCTION__, __LINE__);
    HR_LOGE("%s(%d): str:%s\n", __FUNCTION__, __LINE__, str);
    printf("{ \"%s\": %s }\n", method, str);
    free(str);

    return 0;
}
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
    uint32_t id;
    // int rc = -1;
    //  adjust output line buffered mode
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
    HR_LOGE("Object not found... wait ...\n");

    usleep(1000);
#endif
    struct ubus_subscriber sub;
    memset(&sub, 0, sizeof(sub));
    sub.cb = elevatord_subscriber_callback;

    ubus_register_subscriber(_ubus_ctx, &sub);

    do {
        if (0 == ubus_lookup_id(_ubus_ctx, "elevatord", &id)) {
            if (0 == ubus_subscribe(_ubus_ctx, &sub, id)) {
                break;
            }
            fprintf(stderr, "Subscribe failed.\n");
        }
        HR_LOGE("Object not found... wait ...\n");

        usleep(1000 * 1000);

    } while (1);

    // rc = ubus_add_object(_ubus_ctx, &_elevatord_object);
    // if (0 != rc) {
    // HR_LOGE("can not add object %s -> %s\n", _elevatord_object.name, ubus_strerror(rc));
    //}

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
