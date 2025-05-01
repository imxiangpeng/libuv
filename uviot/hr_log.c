/*
 * Copyright (C) 2024 Inspur Group Co., Ltd. Unpublished
 *
 * Inspur Group Co., Ltd.
 * Proprietary & Confidential
 *
 * This source code and the algorithms implemented therein constitute
 * confidential information and may comprise trade secrets of Inspur
 * or its associates, and any use thereof is subject to the terms and
 * conditions of the Non-Disclosure Agreement pursuant to which this
 * source code was originally received.
 */

#include "hr_log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/syscall.h>
#include <sys/syslog.h>
#include <sys/time.h>
#include <sys/types.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#define LOG_BUF_SIZE 1024 * 2

//static FILE* persist_fp = NULL;

int _hr_log_printf(int prio, const char *tag, const char *fmt, ...) {
    int ret = -1;
    (void)prio;

    va_list ap;
    char buf[LOG_BUF_SIZE] = {0};
    char *ptr = buf;
    size_t available = LOG_BUF_SIZE;
    struct tm tm;
    struct timespec ts;

    clock_gettime(CLOCK_REALTIME, &ts);
    (void)localtime_r(&ts.tv_sec, &tm);

    // time
    size_t size = strftime(ptr, available, "%m-%d %H:%M:%S", &tm);
    available -= size;
    ptr += size;
    // append ms
    ret = snprintf(ptr, available, ".%03ld ", ts.tv_nsec / 1000000);
    if (ret < 0) ret = 0;
    available -= (size_t)ret;
    ptr += ret;

    // pid & tid
    ret = snprintf(ptr, available, "%5d %5ld ", getpid(), syscall(SYS_gettid) /*gettid()*/);
    if (ret < 0) ret = 0;
    available -= (size_t)ret;
    ptr += ret;

    // tag
    if (tag) {
        ret = snprintf(ptr, available, "%-8s: ", tag);
        if (ret < 0) ret = 0;
        available -= (size_t)ret;
        ptr += ret;
    }

    va_start(ap, fmt);
    ret = vsnprintf(ptr, available, fmt, ap);
    // when it's too long, trunk and using ...
    if ( ret > 0 && (size_t)ret > available) {
        // overflow
        buf[LOG_BUF_SIZE - 2] = '\n';
        buf[LOG_BUF_SIZE - 3] = '.';
        buf[LOG_BUF_SIZE - 4] = '.';
        buf[LOG_BUF_SIZE - 5] = '.';
    }
    va_end(ap);

#if 1
    printf("%s", buf);
#else
    // syslog(LOG_SYSLOG, "%s", buf);
    if (!persist_fp) {
        char path[256] = "./hrlog-";
        char* ptr = path + strlen(path);
        strftime(ptr, sizeof(path) - strlen(path) - 1, "%Y-%m-%d-%H-%M", &tm);
        strcat(path, ".log");
        printf("path:%s\n", path);
        persist_fp = fopen(path, "w");
        if (!persist_fp) {
            printf("failed create output ..\n");
        }

        setbuf(persist_fp, NULL);
    }
    
    if (persist_fp) {
        fprintf(persist_fp, "%s", buf);
    }
#endif

    return 0;
}