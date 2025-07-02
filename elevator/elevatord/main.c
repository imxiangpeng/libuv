#include <assert.h>
#include <elfutils/libdwfl.h>
#include <libunwind.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>
#include <uv.h>
#include "floor.h"
#include "hr_log.h"
#include "iot/iot.h"
#include "motion.h"
#include "sensor.h"
#include "tui.h"
#include "ubus/uelevatord.h"

#ifndef _UNUSED
#define _UNUSED __attribute__((__unused__))
#endif

#define MAX_BACKTRACE_LINE_LENGTH 512
#define MAX_BACKTRACE_DEPTH 16

static int _exit_request = 0;

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

    if (SIGTERM == signum) {
        uv_stop(uv_default_loop());
        uv_async_send(&_dummy_keep_loop);
        uv_close((uv_handle_t*)&_dummy_keep_loop, NULL);
    }
}
#if 0
static void xx() {

Dwfl *dwfl = dwfl_begin(&callbacks);
    if (!dwfl) {
        fprintf(stderr, "dwfl_begin failed: %s\n", dwfl_errmsg(-1));
        return;
    }

    if (dwfl_linux_proc_report(dwfl, pid) != 0) {
        fprintf(stderr, "dwfl_linux_proc_report failed: %s\n", dwfl_errmsg(-1));
        dwfl_end(dwfl);
        return;
    }

    dwfl_report_end(dwfl, NULL, NULL);
}
#endif
static void _signal_action_backtrace(int signum, siginfo_t* siginfo, void* sigcontext) {
    (void)siginfo;
    (void)sigcontext;

    HR_LOGD("%s(%d): ........signum:%d\n", __FUNCTION__, __LINE__, signum);

    char line[MAX_BACKTRACE_LINE_LENGTH] = {0};
    ucontext_t* uct = sigcontext;
    uint32_t i = 0, skip = 1;
    unw_cursor_t cursor;
    unw_context_t uc;

    (void)uct;
    signal(signum, SIG_DFL);

    unw_getcontext(&uc);
    unw_init_local(&cursor, &uc);
    // unw_init_local2(&cursor, &uc, UNW_INIT_SIGNAL_FRAME);

    // dont assign reg manual which leading loss stack
#if 0  // def __arm__
    unw_set_reg(&cursor, UNW_ARM_R0, uct->uc_mcontext.arm_r0);
    unw_set_reg(&cursor, UNW_ARM_R1, uct->uc_mcontext.arm_r1);
    unw_set_reg(&cursor, UNW_ARM_R2, uct->uc_mcontext.arm_r2);
    unw_set_reg(&cursor, UNW_ARM_R3, uct->uc_mcontext.arm_r3);
    unw_set_reg(&cursor, UNW_ARM_R4, uct->uc_mcontext.arm_r4);
    unw_set_reg(&cursor, UNW_ARM_R5, uct->uc_mcontext.arm_r5);
    unw_set_reg(&cursor, UNW_ARM_R6, uct->uc_mcontext.arm_r6);
    unw_set_reg(&cursor, UNW_ARM_R7, uct->uc_mcontext.arm_r7);
    unw_set_reg(&cursor, UNW_ARM_R8, uct->uc_mcontext.arm_r8);
    unw_set_reg(&cursor, UNW_ARM_R9, uct->uc_mcontext.arm_r9);
    unw_set_reg(&cursor, UNW_ARM_R10, uct->uc_mcontext.arm_r10);
    unw_set_reg(&cursor, UNW_ARM_R11, uct->uc_mcontext.arm_fp);
    unw_set_reg(&cursor, UNW_ARM_R12, uct->uc_mcontext.arm_ip);
    unw_set_reg(&cursor, UNW_ARM_R13, uct->uc_mcontext.arm_sp);
    unw_set_reg(&cursor, UNW_ARM_R14, uct->uc_mcontext.arm_lr);
    unw_set_reg(&cursor, UNW_ARM_R15, uct->uc_mcontext.arm_pc);
    unw_set_reg(&cursor, UNW_REG_IP, uct->uc_mcontext.arm_pc);
    unw_set_reg(&cursor, UNW_REG_SP, uct->uc_mcontext.arm_sp);
#endif

    HR_LOGE("RECV SIGNAL(%d): %d\n", getpid(), signum);

    do {
        unw_word_t pc;
        _UNUSED unw_word_t offset;
        char sym[256] = {0};
        char filename[256] = {0};

        unw_get_reg(&cursor, UNW_REG_IP, &pc);

        // skip first(our self): _signal_action, until got signal frame
        if (skip == 1) {
            if (unw_is_signal_frame(&cursor)) {
                skip = 0;
            }
            // HR_LOGD("skip before signal pc:%08x ......\n", pc);
            continue;
        }

        unw_get_proc_name(&cursor, sym, sizeof(sym), &offset);

       //  unw_get_elf_filename(&cursor, filename, sizeof(filename), NULL);

        snprintf(line, sizeof(line), "#%02u pc %08lx %.*s (%.*s+%ld)", i, pc, 128 /**/, filename, 60, sym, offset);
        HR_LOGE("%s\n", line);

        i++;
    } while (unw_step(&cursor) > 0 && i < MAX_BACKTRACE_DEPTH);

    exit(EXIT_FAILURE);
}

static int elevatord_main(int argc, char** argv) {
    int is_calibration = 0;
    int base_floor = 1;
    int floors_below_base = 0;
    int floors_above_base = 0;

    HR_LOGD("elevatord %s\n", ELEVATORD_BUILD_TIMESTAMP);

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

    // printf("is_calibration:%d\n", is_calibration);

    struct sigaction action;
    memset(&action, 0, sizeof(action));
    sigemptyset(&action.sa_mask);
    action.sa_sigaction = _signal_action;
    sigaction(SIGTERM, &action, NULL);

    memset(&action, 0, sizeof(action));
    sigemptyset(&action.sa_mask);
    action.sa_sigaction = _signal_action_backtrace;
    action.sa_flags = SA_RESTART | SA_SIGINFO;

    sigaction(SIGABRT, &action, NULL);
    sigaction(SIGBUS, &action, NULL);
    sigaction(SIGFPE, &action, NULL);
    sigaction(SIGILL, &action, NULL);
    sigaction(SIGPIPE, &action, NULL);
    sigaction(SIGSEGV, &action, NULL);
#if defined(SIGSTKFLT)
    sigaction(SIGSTKFLT, &action, NULL);
#endif
    sigaction(SIGTRAP, &action, NULL);

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

    // must called after motion_initalize
    uelevatord_init();

    uv_async_init(uv_default_loop(), &_dummy_keep_loop, dummy_cb);
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

static void _daemon_signal_action(int signum, siginfo_t* siginfo, void* sigcontext) {
    (void)siginfo;
    (void)sigcontext;

    HR_LOGD("%s(%d): ........signum:%d\n", __FUNCTION__, __LINE__, signum);

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

    struct sigaction action;

    HR_LOGD("elevatord %s\n", ELEVATORD_BUILD_TIMESTAMP);

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

            return elevatord_main(argc, argv);
        }

        HR_LOGD("elevatord main started:%d\n", pid);

        waitpid(pid, &status, 0);

        if (WIFEXITED(status)) {
            HR_LOGE("Service %d exited with code %d\n", pid, WEXITSTATUS(status));
        } else if (WIFSIGNALED(status)) {
            HR_LOGE("Service %d killed by signal %d\n", pid, WTERMSIG(status));
        } else {
            HR_LOGE("Service %d exited unexpectedly\n", pid);
        }
    }
    return 0;
}