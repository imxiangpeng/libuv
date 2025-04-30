#include <assert.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <uv.h>
#include "hr_log.h"
#include "iot.h"
#include "uelevator.h"

/*static void dummy_cb(uv_async_t* handle) {
    (void)handle;
    printf("%s(%d): ..........\n", __FUNCTION__, __LINE__);
}*/
//static uv_async_t _dummy_keep_loop;
/* Fully close a loop */
static void close_walk_cb(uv_handle_t* handle, void* arg) {
    (void)arg;
    printf("%s(%d): ..........\n", __FUNCTION__, __LINE__);
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

    if (SIGUSR1 == signum) {
        uv_stop(uv_default_loop());
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


    iot_init();

    elevator_ubus_init();
    
    iot_run(uv_default_loop());
    
    
   // uv_async_init(uv_default_loop(), &_dummy_keep_loop, dummy_cb);
    printf("%s(%d): ..........\n", __FUNCTION__, __LINE__);
    uv_run(uv_default_loop(), UV_RUN_DEFAULT);
    
    
    printf("%s(%d): ..........\n", __FUNCTION__, __LINE__);
    elevator_ubus_deinit();

    printf("%s(%d): ..........\n", __FUNCTION__, __LINE__);
    iot_deinit();

    printf("%s(%d): ..........\n", __FUNCTION__, __LINE__);
    // run once after iot_finally release resource
    uv_run(uv_default_loop(), UV_RUN_DEFAULT);

    MAKE_VALGRIND_HAPPY(uv_default_loop());
    return 0;
}
