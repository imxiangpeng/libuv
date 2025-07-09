
// mxp, 20250529, eguard(elevator guard)
// implement occlusion and e-bike entering elevator alarms

#include <assert.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/poll.h>
#include <unistd.h>

#include "libubox/blob.h"
#include "libubox/blobmsg.h"
#include "libubox/blobmsg_json.h"
#include "libubox/uloop.h"
#include "libubus.h"
#include "sconf.h"

#define UBUS_SOCK "/tmp/ubus.sock"

#define _UBUS_RETRY_TIMEOUT (2)

#define DTOF_OCCLUSION_DISTANCE_MM 100     // 5cm // 30cm
#define EGUARD_ALARM_CONFIRM_TIMEOUT 2000  // 2s
#define EGUARD_ALARM_REPEAT_DELAY 5000     // 5s

#define ELEVATOR_ALARM_EVENT_PREFIX "elevator.event."

enum message {
    MSG_QUIT = 0,
    EVENT_DTOF_DISTANCE_ALARM,
    EVENT_DTOF_DISTANCE_RESUME,
};

enum alarm {
    ALARM_NONE = 0,
    ALARM_DTOF = 1,
    ALARM_EBIKE = 1 << 1,
    ALARM_KUNREN = 1 << 2,
};

static uint32_t _alarm = ALARM_NONE;

static struct ubus_context* _ubus_ctx = NULL;
static int _request_exit = 0;
static int _pipefd[2] = {-1, -1};
static int _playback_pipefd[2] = {-1, -1};

static struct blob_buf _b;

static pthread_t _dtof_tid = -1;
static pthread_t _playback_tid = -1;

static struct uloop_timeout _alarm_timer;

const char* _cared_ubus_event[] = {
    "ubus.object.*",
    ELEVATOR_ALARM_EVENT_PREFIX "*",
};

// you should adjust the array order
// we will prefer play the first matched sound
struct alarm_sound {
    enum alarm alarm;
    const char* sound;
} _alarm_sounds[] = {
    // 请勿遮挡相机谢谢合作.wav
    {ALARM_DTOF, "./alarm_dtof.wav"},
    // 为了你和他人的安全，请勿将电瓶车驶入电梯，谢谢合作
    {ALARM_EBIKE, "./alarm_ebike.wav"},
    {ALARM_KUNREN, "./alarm_kunren.wav"},
    {ALARM_NONE, NULL},
};
// # 播报次数
enum {
    OPTION_EGUARD_ALARM_SWITCH = 0,
    OPTION_EGUARD_DTOF_SWITCH,
    OPTION_EGUARD_DTOF_OCCLUSION_DISTANCE,
    OPTION_EGUARD_KUNREN,
    OPTION_EGUARD_,

};
// default not enable

static struct sconf_proto _eguard_options[] = {
    [OPTION_EGUARD_ALARM_SWITCH] = {"EGUARD_ALARM_SWITCH", PROTO_VALUE_INT64, {.int64 = 0}},
    [OPTION_EGUARD_DTOF_SWITCH] = {"EGUARD_DTOF_SWITCH", PROTO_VALUE_INT64, {.int64 = 1}},
    [OPTION_EGUARD_DTOF_OCCLUSION_DISTANCE] = {"EGUARD_DTOF_OCCLUSION_DISTANCE", PROTO_VALUE_INT64, {.int64 = DTOF_OCCLUSION_DISTANCE_MM}},
    [OPTION_EGUARD_KUNREN] = {"EGUARD_KUNREN", PROTO_VALUE_INT64, {.int64 = 0}},
};

static void _alarm_event_confirm(struct uloop_timeout* t);

static void message_post(int which) {
    if (_pipefd[1] == -1) {
        return;
    }
    write(_pipefd[1], &which, sizeof(which));
}

static void _signal_action(int signum, siginfo_t* siginfo, void* sigcontext) {
    (void)siginfo;
    (void)sigcontext;

    printf("%s(%d): ........signum:%d\n", __FUNCTION__, __LINE__, signum);

    if (SIGUSR1 == signum || SIGTERM == signum) {
        _request_exit = 1;

        // wakeup playback thread
        if (_playback_pipefd[1] != -1) {
            int event = 1;
            write(_playback_pipefd[1], &event, sizeof(event));
        }

        message_post(MSG_QUIT);
    }
}

static int read_sensor_data(int* distance, int* confidence, int* count) {
    FILE* fp = fopen("/sys/class/nds03/nds03", "r");
    if (!fp) {
        return -1;
    }

    if (fscanf(fp, "%d %d %d", distance, confidence, count) != 3) {
        fclose(fp);
        return -1;
    }

    fclose(fp);
    return 0;
}

static const char* get_alarm_sound(enum alarm a) {
    const char* sound = NULL;
    for (size_t i = 0; i < sizeof(_alarm_sounds) / sizeof(_alarm_sounds[0]); i++) {
        if (_alarm_sounds[i].alarm == a) {
            sound = _alarm_sounds[i].sound;
            break;
        }
    }
    return sound;
}

// match the first alarm in _alarm_sounds array
static const char* get_alarm_sound_with_priority(uint32_t alarm) {
    const char* sound = NULL;
    for (size_t i = 0; i < sizeof(_alarm_sounds) / sizeof(_alarm_sounds[0]); i++) {
        if ((_alarm_sounds[i].alarm & alarm) != 0) {
            sound = _alarm_sounds[i].sound;
            break;
        }
    }
    return sound;
}

static void* _playback_thread_routin(void* arg) {
    (void)arg;
    int event = -1;

    char cmd[256] = {0};

    while (_request_exit != 1) {
        const char* sound = NULL;

        struct pollfd fds = {
            .fd = _playback_pipefd[0],
            .events = POLLIN,
        };

        int ret = poll(&fds, 1, EGUARD_ALARM_REPEAT_DELAY);
        if (ret < 0) {
            printf("%s(%d): error ...\n", __FUNCTION__, __LINE__);
            continue;
        } else if (ret == 1) {
            size_t rc = read(_playback_pipefd[0], &event, sizeof(event));
            if (rc != sizeof(event)) {
                continue;
            }
            printf("%s(%d): alarm:0x%x\n", __FUNCTION__, __LINE__, _alarm);
        }

        if (_request_exit == 1) {
            break;
        }
        // == 0, timeout, play repeat!

        if (_alarm == ALARM_NONE) {
            continue;
        }

        // we should confirm event is exist!
        // use global _alarm variable
        sound = get_alarm_sound_with_priority(_alarm);

        if (!sound) {
            continue;
        }

        if (_eguard_options[OPTION_EGUARD_ALARM_SWITCH].value.int64 == 0) {
            printf("eguard is not enabled\n");
            continue;
        }

        memset((void*)cmd, 0, sizeof(cmd));
        // snprintf(cmd, sizeof(cmd), "ffmpeg -hide_banner -i %s -f wav - | aplay", path);
        snprintf(cmd, sizeof(cmd), "aplay -q %s", sound);
        system(cmd);

        printf("play end ...\n");
    }

    return NULL;
}
// you should cancel timer within
static void _alarm_event_confirm(struct uloop_timeout* t) {
    if (!t)
        return;

    // it will do nothing when eguard_alarm is started
    // so we can call repeated
    // hrsvc start eguard_alarm

    // wakeup playback thread directly
    // playback will auto detect event type
    if (_playback_pipefd[1] != -1) {
        int event = 1;
        write(_playback_pipefd[1], &event, sizeof(event));
    }
}

static void* _dtof_detector_thread_routin(void* arg) {
    (void)arg;
    int distance = 0, confidence = 0, count = 0;

    while (_request_exit != 1) {
        if (read_sensor_data(&distance, &confidence, &count) == 0) {
            printf("Distance: %d mm, Confidence: %d, Count: %d\n",
                   distance, confidence, count);

            if (_eguard_options[OPTION_EGUARD_DTOF_SWITCH].value.int64 != 0) {
                if (distance < _eguard_options[OPTION_EGUARD_DTOF_OCCLUSION_DISTANCE].value.int64 && confidence > 90) {
                    message_post(EVENT_DTOF_DISTANCE_ALARM);
                } else {
                    message_post(EVENT_DTOF_DISTANCE_RESUME);
                }
            }
        }
        usleep(1000 * 200);
    }
    return NULL;
}

static void _pipe_uloop_main_thread_handler(struct uloop_fd* u, unsigned int events) {
    (void)u;
    (void)events;
    int which = -1;

    size_t rc = read(_pipefd[0], &which, sizeof(which));
    if (rc != sizeof(which)) {
        return;
    }

    switch (which) {
        case MSG_QUIT:
            printf("receive message:%d quit\n", which);
            uloop_end();
            break;
        case EVENT_DTOF_DISTANCE_ALARM:
            if (0 == (_alarm & ALARM_DTOF)) {
                printf("alarm ...\n");
                _alarm |= ALARM_DTOF;
                _alarm_timer.cb = _alarm_event_confirm;
                uloop_timeout_set(&_alarm_timer, EGUARD_ALARM_CONFIRM_TIMEOUT);
            }
            break;
        case EVENT_DTOF_DISTANCE_RESUME:
            if (0 != (_alarm & ALARM_DTOF)) {
                printf("resume ...\n");
                _alarm &= ~ALARM_DTOF;
                uloop_timeout_cancel(&_alarm_timer);
            }
            break;
        default:
            break;
    }
}

static void ubus_event_handler(struct ubus_context* ctx,
                               struct ubus_event_handler* ev,
                               const char* type,
                               struct blob_attr* msg) {
    (void)ctx;
    (void)ev;
    (void)type;
    (void)msg;

    if (!type) {
        return;
    }

    char* str = blobmsg_format_json(msg, true);
    printf("%s(%d) %s: %s\n", __FUNCTION__, __LINE__, type, str);
    free(str);

    if (0 == strncmp(type, ELEVATOR_ALARM_EVENT_PREFIX, strlen(ELEVATOR_ALARM_EVENT_PREFIX))) {
        const char* event = type + strlen(ELEVATOR_ALARM_EVENT_PREFIX);
        printf("%s(%d): type:%s -> %s\n", __FUNCTION__, __LINE__, type, event);
        if (0 == strcmp("ebike", event)) {
            struct blob_attr* tb[2] = {NULL};
            static const struct blobmsg_policy policy[] = {
                {.name = "status", .type = BLOBMSG_TYPE_INT32},
                {NULL, BLOBMSG_TYPE_UNSPEC},
            };
            blobmsg_parse(policy, sizeof(policy) / sizeof(policy[0]), tb, blobmsg_data(msg),
                          blobmsg_data_len(msg));

            if (!tb[0]) {
                return;
            }

            int status = blobmsg_get_u32(tb[0]);
            if (status == 0) {
                if (0 != (_alarm & ALARM_EBIKE)) {
                    printf("ebike resume ...\n");
                    _alarm &= ~ALARM_EBIKE;
                    uloop_timeout_cancel(&_alarm_timer);
                }
            } else {
                if (0 == (_alarm & ALARM_EBIKE)) {
                    printf("ebike alarm ...\n");
                    _alarm |= ALARM_EBIKE;
                    _alarm_timer.cb = _alarm_event_confirm;
                    uloop_timeout_set(&_alarm_timer, EGUARD_ALARM_CONFIRM_TIMEOUT);
                }
            }
        } else if (0 == strcmp("fault", event)) {
            uint32_t fault = 0;
            const char* type = NULL;
            int status = 0;
            struct blob_attr* tb[3] = {NULL};
            static const struct blobmsg_policy policy[] = {
                {.name = "type", .type = BLOBMSG_TYPE_STRING},
                {.name = "status", .type = BLOBMSG_TYPE_INT32},
                {NULL, BLOBMSG_TYPE_UNSPEC},
            };

            blobmsg_parse(policy, sizeof(policy) / sizeof(policy[0]), tb, blobmsg_data(msg),
                          blobmsg_data_len(msg));

            if (!tb[0] || !tb[1]) {
                return;
            }

            type = blobmsg_get_string(tb[0]);
            status = blobmsg_get_u32(tb[1]);

            if (!type) {
                return;
            }

            printf("type:%s, fault:%d, status:%d\n", type, fault, status);

            if (0 == strcmp("kunren", type)) {
                if (status == 0) {
                    if (0 != (_alarm & ALARM_KUNREN)) {
                        printf("kunren resume ...\n");
                        _alarm &= ~ALARM_KUNREN;
                        uloop_timeout_cancel(&_alarm_timer);
                    }
                } else {
                    if (0 == (_alarm & ALARM_KUNREN)) {
                        printf("kunren alarm ...\n");
                        _alarm |= ALARM_KUNREN;
                        _alarm_timer.cb = _alarm_event_confirm;
                        uloop_timeout_set(&_alarm_timer, EGUARD_ALARM_CONFIRM_TIMEOUT);
                    }
                }
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

    // we should re subscriber event?

    for (size_t i = 0; i < sizeof(_cared_ubus_event) / sizeof(_cared_ubus_event[0]); i++) {
        ubus_register_event_handler(_ubus_ctx, &_ubus_event, _cared_ubus_event[i]);
    }

    ubus_add_uloop(_ubus_ctx);

#ifdef FD_CLOEXEC
    fcntl(_ubus_ctx->sock.fd, F_SETFD, fcntl(_ubus_ctx->sock.fd, F_GETFD) | FD_CLOEXEC);
#endif
}

static void _connection_lost(struct ubus_context* ctx) {
    (void)ctx;
    _reconnect_timer(NULL);
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    pthread_attr_t attr;

    struct sigaction action;
    memset(&action, 0, sizeof(action));
    sigemptyset(&action.sa_mask);
    action.sa_sigaction = _signal_action;
    sigaction(SIGUSR1, &action, NULL);
    sigaction(SIGTERM, &action, NULL);

    memset((void*)&_alarm_timer, 0, sizeof(_alarm_timer));
    memset((void*)&_b, 0, sizeof(_b));

    sconf_load_with_proto(EGUARD_CONFIG_PATH, _eguard_options, sizeof(_eguard_options) / sizeof(_eguard_options[0]));

    if (0 != pipe(_pipefd)) {
        return -1;
    }

    if (0 != pipe(_playback_pipefd)) {
        close(_pipefd[0]);
        close(_pipefd[1]);

        _pipefd[0] = -1;
        _pipefd[1] = -1;
        return -1;
    }

    fcntl(_playback_pipefd[0], F_SETFL, fcntl(_playback_pipefd[0], F_GETFL) | O_NONBLOCK);

    pthread_attr_init(&attr);

    // pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&_dtof_tid, &attr, _dtof_detector_thread_routin, NULL);
    pthread_create(&_playback_tid, &attr, _playback_thread_routin, NULL);

    struct uloop_fd pipe_fd = {
        .fd = _pipefd[0],
        .cb = _pipe_uloop_main_thread_handler,
    };

    blob_buf_init(&_b, 0);

    uloop_init();

    while (_request_exit != 1) {
        _ubus_ctx = ubus_connect(UBUS_SOCK);
        if (_ubus_ctx) {
            break;
        }
        usleep(1000 * 1000);
        // no need call testcancel because usleep is cancel point
        pthread_testcancel();
    }

    _ubus_ctx->connection_lost = _connection_lost;

    ubus_add_uloop(_ubus_ctx);

#ifdef FD_CLOEXEC
    fcntl(_ubus_ctx->sock.fd, F_SETFD, fcntl(_ubus_ctx->sock.fd, F_GETFD) | FD_CLOEXEC);
#endif

    for (size_t i = 0; i < sizeof(_cared_ubus_event) / sizeof(_cared_ubus_event[0]); i++) {
        ubus_register_event_handler(_ubus_ctx, &_ubus_event, _cared_ubus_event[i]);
    }

    uloop_fd_add(&pipe_fd, ULOOP_READ);

    uloop_run();

    ubus_unregister_event_handler(_ubus_ctx, &_ubus_event);
    ubus_free(_ubus_ctx);
    _ubus_ctx = NULL;

    uloop_done();

    close(_pipefd[0]);
    close(_pipefd[1]);
    _pipefd[0] = -1;
    _pipefd[1] = -1;

    close(_playback_pipefd[0]);
    close(_playback_pipefd[1]);
    _playback_pipefd[0] = -1;
    _playback_pipefd[1] = -1;

    blob_buf_free(&_b);

    pthread_cancel(_dtof_tid);
    pthread_cancel(_playback_tid);
    pthread_join(_dtof_tid, NULL);
    pthread_join(_playback_tid, NULL);

    return 0;
}
