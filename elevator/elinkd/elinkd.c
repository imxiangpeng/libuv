
// mxp, 20250703, extract the IoT management module from elevatord.

#include "elinkd.h"

#include "libubox/uloop.h"
#define _GNU_SOURCE
#include <assert.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#include "elevator.h"
#include "hr_log.h"
#include "iot.h"
#include "libubox/blob.h"
#include "libubox/blobmsg.h"
#include "libubox/blobmsg_json.h"
#include "libubox/list.h"
#include "libubus.h"
#include "topic.h"

#define UBUS_SOCK "/tmp/ubus.sock"

#define ELEVATORD_NAME "elevatord"

#define ELEVATORD_EVENT_REALTIME "RealTime"
#define ELEVATORD_EVENT_HISTORICAL "Historical"
#define ELEVATORD_EVENT_MOTION "Motion"

#define ELEVATOR_EVENT_PREFIX "elevator.event."

#define _UBUS_RETRY_TIMEOUT (2)

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

#ifndef HR_LOGD
#define HR_LOGD printf
#endif

#ifndef HR_LOGE
#define HR_LOGE printf
#endif

enum {
    MSG_REALTIME,
    MSG_HISTORICAL,
    MSG_QUIT,
    MSG_IOT_INIT,
    MSG_ULOOP_TIMEOUT_SET,
    MSG_ULOOP_TIMEOUT_CANCEL,
    MSG_POST_ASYNC_TASK,
};

// mxp, 20231029, simple timer task using libubox
struct _inner_task {
    task_handler task;
    void* arg;
};

struct _timer_task {
    struct uloop_timeout timeout;
    struct _inner_task data;
};

static struct ubus_context* _ubus_ctx = NULL;

static int _request_exit = 0;
static int _pipefd[2] = {-1, -1};  // [0]=read, [1]=write

static struct ubus_subscriber _elevatord_subscriber;
static uint32_t _elevatord_object_id = 0;
static struct blob_buf _b;

static void _timeout_task_cb(struct uloop_timeout* t) {
    struct _timer_task* task = (struct _timer_task*)t;
    if (!task) {
        return;
    }

    if (task->data.task) {
        // ignore result
        task->data.task(task->data.arg);
    }

    memset((void*)task, 0, sizeof(*task));
    free(task);
}

// we will auto release memory which allocated here when task doned
int post_timer_task(int msec, task_handler task, void* arg) {
    // memory will auto be release when fired
    struct _timer_task* t = NULL;

    if (msec < 0) {
        return -1;
    }

    t = (struct _timer_task*)calloc(sizeof(struct _timer_task), 1);
    if (!t) {
        return -1;
    }

    t->timeout.cb = _timeout_task_cb;

    t->data.task = task;
    t->data.arg = arg;

    uloop_timeout_set(&t->timeout, msec);

    return 0;
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
        case MSG_IOT_INIT: {
            HR_LOGD("iot init:%d !\n", which);

            if (iot_init() != 0) {
                HR_LOGD("iot init failed\n");
            }

            break;
        }
        case MSG_ULOOP_TIMEOUT_SET: {
            struct uloop_timeout* t = NULL;
            int msec = 0;

            read(_pipefd[0], &t, sizeof(t));
            read(_pipefd[0], &msec, sizeof(msec));
            if (!t) {
                break;
            }

            uloop_timeout_set(t, msec);

            break;
        }
        case MSG_ULOOP_TIMEOUT_CANCEL: {
            struct uloop_timeout* t = NULL;

            read(_pipefd[0], &t, sizeof(t));

            if (!t) {
                break;
            }

            uloop_timeout_cancel(t);

            break;
        }
        case MSG_POST_ASYNC_TASK: {
            task_handler task = NULL;
            void* args = NULL;

            read(_pipefd[0], &task, sizeof(task));
            read(_pipefd[0], &args, sizeof(args));

            task(args);
            break;
        }
    }
}

static void post_message(int which) {
    write(_pipefd[1], &which, sizeof(which));
}

void timer_post(struct uloop_timeout* t, int msec) {
    struct {
        int which;
        struct uloop_timeout* t;
        int msec;
    } __attribute__((packed)) data = {MSG_ULOOP_TIMEOUT_SET, t, msec};
    write(_pipefd[1], &data, sizeof(data));
}

void timer_cancel(struct uloop_timeout* t) {
    struct {
        int which;
        struct uloop_timeout* t;
    } __attribute__((packed)) data = {MSG_ULOOP_TIMEOUT_CANCEL, t};
    write(_pipefd[1], &data, sizeof(data));
}

int post_async_task(task_handler task, void* args) {
    struct {
        int which;
        task_handler task;
        void* priv;
    } __attribute__((packed)) data = {MSG_POST_ASYNC_TASK, task, args};
    write(_pipefd[1], &data, sizeof(data));

    return 0;
}

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

/*static const struct blobmsg_policy realtime_policy[__RT_MAX] = {
    [RT_ACCEL] = {.name = "accel", .type = BLOBMSG_TYPE_DOUBLE},
    [RT_SPEED] = {.name = "velocity", .type = BLOBMSG_TYPE_DOUBLE},
    [RT_DISTANCE] = {.name = "distance", .type = BLOBMSG_TYPE_DOUBLE},
    [RT_DIRECTION] = {.name = "direction", .type = BLOBMSG_TYPE_INT32},
    [RT_FLOOR] = {.name = "floor", .type = BLOBMSG_TYPE_INT32},
    [RT_JITTER_FREQ] = {.name = "jitter_freq", .type = BLOBMSG_TYPE_DOUBLE},
    [RT_JITTER_ACCEL] = {.name = "jitter_accel", .type = BLOBMSG_TYPE_DOUBLE},
};*/

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
static int subscriber_elevatord_event() {
    if (_elevatord_object_id != 0) {
        // have subscribed
        return 0;
    }
    if (0 == ubus_lookup_id(_ubus_ctx, ELEVATORD_NAME, &_elevatord_object_id)) {
        if (0 == ubus_subscribe(_ubus_ctx, &_elevatord_subscriber, _elevatord_object_id)) {
            elevator_elevatord_connected(_ubus_ctx, _elevatord_object_id);
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
    // char* str = blobmsg_format_json(msg, true);
    // HR_LOGD("%s(%d) %s: %s\n", __FUNCTION__, __LINE__, type, str);
    // free(str);

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
    }

    elevator_ubus_event_handler(ctx, ev, type, msg);
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

    // we should re subscriber event?

    _elevatord_object_id = 0;
    ubus_register_subscriber(_ubus_ctx, &_elevatord_subscriber);

    ubus_register_event_handler(_ubus_ctx, &_ubus_event, "ubus.object.*");
    ubus_register_event_handler(_ubus_ctx, &_ubus_event, ELEVATOR_EVENT_PREFIX "*");

    subscriber_elevatord_event();

    ubus_add_uloop(_ubus_ctx);

#ifdef FD_CLOEXEC
    fcntl(_ubus_ctx->sock.fd, F_SETFD, fcntl(_ubus_ctx->sock.fd, F_GETFD) | FD_CLOEXEC);
#endif
}

static void _connection_lost(struct ubus_context* ctx) {
    (void)ctx;
    _reconnect_timer(NULL);
}
static void _signal_action(int signum, siginfo_t* siginfo, void* sigcontext) {
    (void)siginfo;
    (void)sigcontext;

    HR_LOGD("%s(%d): ........signum:%d\n", __FUNCTION__, __LINE__, signum);
    printf("%s(%d): ........signum:%d\n", __FUNCTION__, __LINE__, signum);

    if (SIGTERM == signum) {
        post_message(MSG_QUIT);
    }
}
int elinkd_main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    struct sigaction action;

    HR_LOGD("elinkd %s\n", BUILD_TIMESTAMP);

    memset(&action, 0, sizeof(action));
    sigemptyset(&action.sa_mask);
    action.sa_sigaction = _signal_action;
    sigaction(SIGTERM, &action, NULL);

    if (pipe(_pipefd) < 0) {
        perror("pipe");
        return -1;
    }

    struct uloop_fd pipe_fd = {
        .fd = _pipefd[0],
        .cb = _pipe_uloop_main_thread_handler,
    };

    memset((void*)&_b, 0, sizeof(_b));
    blob_buf_init(&_b, 0);

    uloop_init();

    while (_request_exit != 1) {
        _ubus_ctx = ubus_connect(UBUS_SOCK);
        if (_ubus_ctx) {
            break;
        }
        HR_LOGD("%s(%d): can not connect ubusd!\n", __FUNCTION__, __LINE__);
        usleep(1000 * 1000);
    }

    printf("ubus connect .....\n");

    _ubus_ctx->connection_lost = _connection_lost;

    ubus_add_uloop(_ubus_ctx);

#ifdef FD_CLOEXEC
    fcntl(_ubus_ctx->sock.fd, F_SETFD, fcntl(_ubus_ctx->sock.fd, F_GETFD) | FD_CLOEXEC);
#endif

    memset(&_elevatord_subscriber, 0, sizeof(_elevatord_subscriber));
    _elevatord_subscriber.cb = elevator_elevatord_subscriber_callback;

    ubus_register_subscriber(_ubus_ctx, &_elevatord_subscriber);

    ubus_register_event_handler(_ubus_ctx, &_ubus_event, "ubus.object.*");
    ubus_register_event_handler(_ubus_ctx, &_ubus_event, ELEVATOR_EVENT_PREFIX "*");

    subscriber_elevatord_event();

    uloop_fd_add(&pipe_fd, ULOOP_READ);

    post_message(MSG_IOT_INIT);
    uloop_run();

    iot_deinit();
    ubus_unregister_event_handler(_ubus_ctx, &_ubus_event);
    ubus_unregister_subscriber(_ubus_ctx, &_elevatord_subscriber);
    ubus_free(_ubus_ctx);
    _ubus_ctx = NULL;

    uloop_done();

    blob_buf_free(&_b);
    return 0;
}
