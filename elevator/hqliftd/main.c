#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <uv.h>
#include "elevator.h"
#include "hr_log.h"
#include "iot.h"
#include "state_machine.h"
#include "uelevator.h"

static uv_async_t _dummy_keep_loop;

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

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    const char* serial = NULL;
    const char* elevator_no = NULL;

    struct sigaction action;
    memset(&action, 0, sizeof(action));
    sigemptyset(&action.sa_mask);
    action.sa_sigaction = _signal_action;
    sigaction(SIGUSR1, &action, NULL);
    sigaction(SIGTERM, &action, NULL);

    serial = elevator_serialno();
    elevator_no = elevator_deviceid();

    // houqi require the serial number to be at least 12 characters long.
    if (!serial || strlen(serial) <= 12) {
        HR_LOGE("serial is invalid!\n");
        return -1;
    }

    if (!elevator_no || strlen(elevator_no) < 3) {
        HR_LOGE("elevator no is invalid!\n");
        return -1;
    }

    elevator_init();

    uelevator_init();

    // block until connected
    iot_init(uv_default_loop());

    statemachine_init(uv_default_loop());
    uv_async_init(uv_default_loop(), &_dummy_keep_loop, dummy_cb);
    uv_run(uv_default_loop(), UV_RUN_DEFAULT);

    statemachine_deinit();
    iot_deinit();

    uelevator_deinit();

    // run once after iot_finally release resource
    uv_run(uv_default_loop(), UV_RUN_DEFAULT);

    MAKE_VALGRIND_HAPPY(uv_default_loop());
    return 0;
}
