#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <uv.h>
#include "hr_log.h"

#define DTOF_DISTANCE_THRESHOLD_MM 300   // 30cm
#define DTOF_ALARM_CONFIRM_TIMEOUT 2000  // 2s
#define ALARM_REPEAT_TIMEOUT 10000       // 10s

enum message {
    EVENT_DTOF_DISTANCE_ALARM,
    EVENT_DTOF_DISTANCE_RESUME,
};

enum alarm {
    ALARM_NONE = 0,
    ALARM_DTOF = 1,
    ALARM_ELECTRIC_BICYCLE = 1 << 1,
};

static enum alarm _alarm = ALARM_NONE;

static int _pipefd[2] = {-1};
static uv_poll_t _message_queue_poll;
static uv_timer_t _dtof_detector_timer;
static uv_thread_t _dtof_thread;
static uv_work_t _play_work;

static uv_async_t _dummy_keep_loop;

static void _dtof_detector_alarm_confirm(uv_timer_t* handle);

static void dummy_cb(uv_async_t* handle) {
    (void)handle;
}

/* Fully close a loop */
static void close_walk_cb(uv_handle_t* handle, void* arg) {
    (void)arg;
    if (!uv_is_closing(handle)) {
        uv_close(handle, NULL);
    }
}

static void close_loop(uv_loop_t* loop) {
    (void)loop;
    uv_walk(loop, close_walk_cb, NULL);
    uv_run(loop, UV_RUN_DEFAULT);
}
#define MAKE_VALGRIND_HAPPY(loop)         \
    do {                                  \
        close_loop(loop);                 \
        assert(0 == uv_loop_close(loop)); \
        uv_library_shutdown();            \
    } while (0)

static void _signal_action(int signum, siginfo_t* siginfo, void* sigcontext) {
    (void)siginfo;
    (void)sigcontext;

    HR_LOGD("%s(%d): ........signum:%d\n", __FUNCTION__, __LINE__, signum);

    if (SIGUSR1 == signum || SIGTERM == signum) {
        uv_stop(uv_default_loop());
        uv_async_send(&_dummy_keep_loop);
        uv_close((uv_handle_t*)&_dummy_keep_loop, NULL);
    }
}

static int message_post(enum message message) {
    if (_pipefd[1] == -1) {
        return -1;
    }

    write(_pipefd[1], &message, sizeof(message));
    return 0;
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

static int _play_work_busy = 0;

static void _play_work_cb(uv_work_t* req) {
    (void)req;
    const char* path = "/data/notice.wav";
    char cmd[256] = {0};
    HR_LOGD("play ...\n");
    // snprintf(cmd, sizeof(cmd), "ffmpeg -hide_banner -i %s -f wav - | aplay", path);
    snprintf(cmd, sizeof(cmd), "aplay %s", path);
    system(cmd);
}

static void _after_play_work_cb(uv_work_t* req, int status) {
    (void)req;
    (void)status;
    HR_LOGD("finished ...\n");
    _play_work_busy = 0;

    if (0 != (_alarm & ALARM_DTOF)) {
        uv_timer_start(&_dtof_detector_timer, _dtof_detector_alarm_confirm, ALARM_REPEAT_TIMEOUT, 0);
    }
}

// you should cancel timer within
static void _dtof_detector_alarm_confirm(uv_timer_t* handle) {
    if (!handle)
        return;

    HR_LOGD("%s(%d): come in...play sound\n", __FUNCTION__, __LINE__);

    // it will do nothing when eguard_alarm is started
    // so we can call repeated
    // hrsvc start eguard_alarm

    if (_play_work_busy == 0) {
        uv_queue_work(handle->loop, &_play_work, _play_work_cb, _after_play_work_cb);
        return;
    }
    uv_timer_start(&_dtof_detector_timer, _dtof_detector_alarm_confirm, 1000, 0);
}

static void _dtof_detector_thread_routin(void* arg) {
    (void)arg;
    int distance = 0, confidence = 0, count = 0;

    printf("%s(%d): come in...\n", __FUNCTION__, __LINE__);

    while (1) {
        if (read_sensor_data(&distance, &confidence, &count) == 0) {
            HR_LOGD("Distance: %d mm, Confidence: %d, Count: %d\n",
                   distance, confidence, count);

            if (confidence > 90) {
                if (distance < DTOF_DISTANCE_THRESHOLD_MM) {
                    message_post(EVENT_DTOF_DISTANCE_ALARM);
                } else {
                    message_post(EVENT_DTOF_DISTANCE_RESUME);
                }
            }
        }
        usleep(1000 * 200);
    }
}

static void _message_queue_handler(uv_poll_t* handle, int status, int events) {
    (void)handle;
    (void)status;
    (void)events;

    int event = -1;

    if (!(events & UV_READABLE)) {
        return;
    }

    size_t rc = read(handle->io_watcher.fd, &event, sizeof(event));
    if (rc != sizeof(event)) {
        return;
    }

    switch (event) {
        case EVENT_DTOF_DISTANCE_ALARM:
            HR_LOGD("alarm ...\n");
            if (0 == (_alarm & ALARM_DTOF)) {
                _alarm |= ALARM_DTOF;
                uv_timer_start(&_dtof_detector_timer, _dtof_detector_alarm_confirm, DTOF_ALARM_CONFIRM_TIMEOUT, 0);
            }
            break;
        case EVENT_DTOF_DISTANCE_RESUME:
            HR_LOGD("resume ...\n");
            _alarm &= ~ALARM_DTOF;
            uv_timer_stop(&_dtof_detector_timer);
            break;
    }
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    struct sigaction action;
    memset(&action, 0, sizeof(action));
    sigemptyset(&action.sa_mask);
    action.sa_sigaction = _signal_action;
    sigaction(SIGUSR1, &action, NULL);
    sigaction(SIGTERM, &action, NULL);

    if (0 != pipe(_pipefd)) {
        return -1;
    }

    uv_poll_init(uv_default_loop(), &_message_queue_poll, _pipefd[0]);
    uv_poll_start(&_message_queue_poll, UV_READABLE, _message_queue_handler);

    uv_timer_init(uv_default_loop(), &_dtof_detector_timer);

    uv_thread_create(&_dtof_thread, _dtof_detector_thread_routin, NULL);
    // uv_timer_start(&_dtof_detector_timer, _dtof_detector_run_once, 1000, 1000);
    //
    uv_run(uv_default_loop(), UV_RUN_DEFAULT);

    uv_timer_stop(&_dtof_detector_timer);
    uv_poll_stop(&_message_queue_poll);
    uv_close((uv_handle_t*)&_message_queue_poll, NULL);

    // run once after iot_finally release resource
    uv_run(uv_default_loop(), UV_RUN_DEFAULT);

    MAKE_VALGRIND_HAPPY(uv_default_loop());

    close(_pipefd[0]);
    close(_pipefd[1]);

    _pipefd[0] = -1;
    _pipefd[1] = -1;

    return 0;
}
