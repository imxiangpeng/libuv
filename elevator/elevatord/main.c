#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <uv.h>
#include "floor.h"
#include "hr_log.h"
#include "iot/iot.h"
#include "motion.h"
#include "sensor.h"
#include "tui.h"
#include "ubus/uelevatord.h"

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
    int is_calibration = 0;
    int base_floor = 1;
    int floors_below_base = 0;
    int floors_above_base = 0;

    argc--;
    argv++;

    while (argc > 0) {
        printf("argv:%s\n", argv[0]);
        if (!strcmp(argv[0], "calibration")) {
            if (argc < 4) {
                HR_LOGE("invalid parameter for calibration\n");
                return -1;
            }

            is_calibration = 1;

            base_floor = atoi(argv[1]);
            floors_below_base = atoi(argv[2]);
            floors_above_base = atoi(argv[3]);

            printf("base:%d, floors below:%d, above:%d\n", base_floor, floors_below_base, floors_above_base);

            argc -= 3;
            argv += 3;
        }

        printf("argc:%d\n", argc);
        argc--;
        argv++;
    }

    printf("is_calibration:%d\n", is_calibration);

    struct sigaction action;
    memset(&action, 0, sizeof(action));
    sigemptyset(&action.sa_mask);
    action.sa_sigaction = _signal_action;
    sigaction(SIGUSR1, &action, NULL);
    sigaction(SIGTERM, &action, NULL);

    sensor_init();

    if (0 != motion_initalize(argc, argv)) {
        printf("error, can not initalize core ...\n");
        return -1;
    }

    floor_init();

    if (is_calibration) {
        floor_enter_calibration(base_floor, floors_below_base, floors_above_base);
    }

    tui_init();

    motion_run();

    uelevatord_init();

    uv_async_init(uv_default_loop(), &_dummy_keep_loop, dummy_cb);
    // iot block until connected
    iot_init(uv_default_loop());
    uv_run(uv_default_loop(), UV_RUN_DEFAULT);

    motion_deinitalize();

    tui_deinit();
    floor_deinit();

    uelevatord_deinit();

    iot_deinit();

    // run once after iot_finally release resource
    uv_run(uv_default_loop(), UV_RUN_DEFAULT);

    MAKE_VALGRIND_HAPPY(uv_default_loop());
    return 0;
}
