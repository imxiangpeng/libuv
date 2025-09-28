#include <assert.h>
#include <curl/curl.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <uv.h>

#include "elevator.h"
#include "hr_log.h"
#include "iot.h"
#include "misc.h"
#include "option.h"
#include "state_machine.h"
#include "uelevator.h"

static int _exit_request = 0;

static uv_async_t _dummy_keep_loop;

static void dummy_cb(uv_async_t* handle) {
    (void)handle;
    pid_t pid = -1;
    int status = 0;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        printf("pid:%d, status:%d\n", pid, status);
        misc_child_exited(pid, status);
    }
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
    printf("%s(%d): ........signum:%d\n", __FUNCTION__, __LINE__, signum);

    // process term early
    if (SIGTERM == signum) {
        uv_stop(uv_default_loop());
        uv_async_send(&_dummy_keep_loop);
        uv_close((uv_handle_t*)&_dummy_keep_loop, NULL);
    }

    // now only support SIGCHLD
    if (SIGCHLD == signum) {
        pid_t pid = -1;
        int status = 0;
        // int block = 0;
        // while ((pid = waitpid(-1, &status, block ? 0 : WNOHANG)) == -1 && errno == EINTR);

        // uv_async_send(&_dummy_keep_loop);
        while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
            printf("hqliftd: -> pid:%d, status:%d\n", pid, status);
            misc_child_exited(pid, status);
        }
    }
}

static int hqliftd_main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    const char* serial = NULL;
    const char* elevator_no = NULL;

    struct sigaction action;

    printf("%s(%d): ........\n", __FUNCTION__, __LINE__);
    HR_LOGD("hqliftd %s\n", HQLIFTD_BUILD_TIMESTAMP);

    memset(&action, 0, sizeof(action));
    sigemptyset(&action.sa_mask);
    action.sa_sigaction = _signal_action;
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGCHLD, &action, NULL);

    if (0 != option_init()) {
        HR_LOGE("hqliftd no valid option config, exit normally, should not start again\n");
        return -1;
    }

    printf("%s(%d): ........\n", __FUNCTION__, __LINE__);
    serial = elevator_serialno();
    elevator_no = elevator_deviceid();

    printf("%s(%d): ........\n", __FUNCTION__, __LINE__);
    // houqi require the serial number to be at least 12 characters long.
    if (!serial || strlen(serial) <= 12) {
        HR_LOGE("serial is invalid!\n");
        return -1;
    }

    if (!elevator_no || strlen(elevator_no) < 3) {
        HR_LOGE("elevator no is invalid!\n");
        return -1;
    }

    printf("%s(%d): ........\n", __FUNCTION__, __LINE__);
    curl_global_init(CURL_GLOBAL_DEFAULT);

    elevator_init();
    printf("%s(%d): ........\n", __FUNCTION__, __LINE__);

    statemachine_init(uv_default_loop());

    printf("%s(%d): ........\n", __FUNCTION__, __LINE__);
    uelevator_init();

    printf("%s(%d): ........\n", __FUNCTION__, __LINE__);
    // block until connected
    if (iot_init(uv_default_loop())) {
        _exit(-1);
    }

    uv_async_init(uv_default_loop(), &_dummy_keep_loop, dummy_cb);
    printf("%s(%d): ........\n", __FUNCTION__, __LINE__);
    uv_run(uv_default_loop(), UV_RUN_DEFAULT);

    printf("%s(%d): ........\n", __FUNCTION__, __LINE__);
    iot_deinit();

    printf("%s(%d): ........\n", __FUNCTION__, __LINE__);
    uelevator_deinit();

    printf("%s(%d): ........\n", __FUNCTION__, __LINE__);
    statemachine_deinit();

    printf("%s(%d): ........\n", __FUNCTION__, __LINE__);
    // run once after iot_finally release resource
    uv_run(uv_default_loop(), UV_RUN_DEFAULT);

    printf("%s(%d): ........\n", __FUNCTION__, __LINE__);
    MAKE_VALGRIND_HAPPY(uv_default_loop());

    printf("%s(%d): ........\n", __FUNCTION__, __LINE__);
    curl_global_cleanup();
    printf("%s(%d): ........\n", __FUNCTION__, __LINE__);
    return 0;
}

static void _daemon_signal_action(int signum, siginfo_t* siginfo, void* sigcontext) {
    (void)siginfo;
    (void)sigcontext;

    HR_LOGD("%s(%d): ........signum:%d\n", __FUNCTION__, __LINE__, signum);
    printf("%s(%d): ...pgrp:%d.....signum:%d\n", __FUNCTION__, __LINE__, getpgrp(), signum);

    if (SIGTERM == signum) {
        if (_exit_request == 0) {
            _exit_request = 1;
            // send to all child process in current process group
            killpg(getpgrp(), SIGTERM);
        }
    }
#if 0
    if (SIGCHLD == signum) {
        pid_t pid;
        int status;
        while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
            if (WIFEXITED(status)) {
                HR_LOGE("Child %d exited with status %d", pid, WEXITSTATUS(status));
            } else if (WIFSIGNALED(status)) {
                HR_LOGE("Child %d killed by signal %d", pid, WTERMSIG(status));
            }
        }
    }
#endif
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;
    const char* serial = NULL;
    const char* elevator_no = NULL;

    struct sigaction action;

    return hqliftd_main(argc, argv);

    // do not call hrlog in parent process

    memset(&action, 0, sizeof(action));
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO | SA_RESTART;
    action.sa_sigaction = _daemon_signal_action;
    sigaction(SIGTERM, &action, NULL);
    // sigaction(SIGINT, &action, NULL);
    sigaction(SIGCHLD, &action, NULL);

    signal(SIGUSR1, SIG_IGN);
    signal(SIGUSR2, SIG_IGN);

    setpgid(0, 0);

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

    while (_exit_request == 0) {
        int status;
        pid_t pid = fork();

        if (pid == 0) {
            setpgid(0, getppid());
            prctl(PR_SET_PDEATHSIG, SIGTERM);

            memset(&action, 0, sizeof(action));
            sigemptyset(&action.sa_mask);
            action.sa_flags = SA_SIGINFO | SA_RESTART;
            action.sa_handler = SIG_DFL;
            sigaction(SIGTERM, &action, NULL);
            sigaction(SIGCHLD, &action, NULL);

            return hqliftd_main(argc, argv);
        }

        HR_LOGD("hqliftd main started:%d\n", pid);

        waitpid(pid, &status, 0);

        if (WIFEXITED(status)) {
            HR_LOGE("Service %d exited with code %d\n", pid, WEXITSTATUS(status));
            if (0 == WEXITSTATUS(status) || 255 == WEXITSTATUS(status)) {
                HR_LOGE("Service %d exited(%d) normally, do not auto restart!\n", pid, WEXITSTATUS(status));
                _exit_request = 1;
            }
        } else if (WIFSIGNALED(status)) {
            HR_LOGE("Service %d killed by signal %d\n", pid, WTERMSIG(status));
        } else {
            HR_LOGE("Service %d exited unexpectedly\n", pid);
        }
    }
    return 0;
}