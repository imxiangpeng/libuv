#include <pthread.h>
#include <stdio.h>

#include "hr_log.h"
#include "libubus.h"
#include "uobject.h"

#define _UBUS_RETRY_TIMEOUT (2)

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

static struct ubus_context* _ubus_ctx = NULL;

static pthread_t _uobject_tid = 0;

extern struct ubus_object _elevatord_object;

static void _reconnect_timer(struct uloop_timeout* timeout) {
    (void)timeout;
    int t = _UBUS_RETRY_TIMEOUT;

    static struct uloop_timeout retry = {
        .cb = _reconnect_timer,
    };

    if (!_ubus_ctx)
        return;

    if (ubus_reconnect(_ubus_ctx, NULL) != 0) {
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
    const char* sock = "/tmp/ubus.sock";
    // adjust output line buffered mode
    setvbuf(stdout, NULL, _IOLBF, 0);

    uloop_init();

    _ubus_ctx = ubus_connect(sock);
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

    uobject_deinit();

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