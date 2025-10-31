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

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <pthread.h>
#include <resolv.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/syslog.h>
#include <sys/time.h>
#include <sys/types.h>
#include <syslog.h>
#include <time.h>
#include <unistd.h>

#include "file_util.h"
#include "sconf.h"

#define LOG_BUF_SIZE LINE_MAX  // 1024 * 2

// #define RSYSLOG_SERVER "1.20222202.xyz"
#define RSYSLOG_SERVER "192.168.58.100"
#define RSYSLOG_PORT "514"

#ifndef HRLOG_CONFIG_PATH
#define HRLOG_CONFIG_PATH "/etc/hrlog.conf"
#endif

enum {
  HRLOG_PROTO_PRINTF = 0,
  HRLOG_PROTO_FILE,
  HRLOG_PROTO_SYSLOG,
  HRLOG_PROTO_RSYSLOG,
};

static char _hostname[256] = {0};

// using static memory, do not dynamic allocate
// per tag name is limited in 16
// 32 * 16
#define TAGS_SIZE 32
#define TAGS_LENGTH 16
static char _tags_filter[TAGS_SIZE][TAGS_LENGTH] = {{0}};

enum {
    OPTION_PROTO = 0,
    OPTION_PRIORITY,
    OPTION_TAGS,
    _OPTION_MAX,
};
static struct sconf_proto _options[_OPTION_MAX] = {
    {"PROTO", PROTO_VALUE_NUMBER, {.number = HRLOG_PROTO_RSYSLOG}},
    {"PRIORITY", PROTO_VALUE_NUMBER, {.number = HR_LOG_WARN}},
    {"TAGS", PROTO_VALUE_STRING, {.string = NULL}},
};

static pthread_once_t persist_once_control = PTHREAD_ONCE_INIT;

static struct rsock {
    int sock;
    struct addrinfo* res;
} _rsyslog = {-1, NULL};

static FILE* persist_fp = NULL;

// static pthread_mutex_t persist_lock = PTHREAD_MUTEX_INITIALIZER;
static void init_persist_output() {
    struct tm tm;
    struct timespec ts;

    char path[256] = "./hrlog-";
    char* ptr = path + strlen(path);

    if (persist_fp) {
        return;
    }

    FILE* f = fopen("/proc/self/comm", "r");
    if (f) {
        fscanf(f, "%s", ptr);
        printf("Process name: %s\n", ptr);
        fclose(f);
    }

    ptr = path + strlen(path);
    sprintf(ptr, "-%d-", getpid());
    ptr = path + strlen(path);

    clock_gettime(CLOCK_REALTIME, &ts);
    (void)localtime_r(&ts.tv_sec, &tm);

    strftime(ptr, sizeof(path) - strlen(path) - 1, "%Y-%m-%d-%H-%M", &tm);
    strcat(path, ".log");
    printf("path:%s\n", path);
    persist_fp = fopen(path, "w");
    if (!persist_fp) {
        printf("failed create output ..\n");
        return;
    }

    setbuf(persist_fp, NULL);
}

// not dynamic update, maybe broken with multi threads, othersize you should use lock
static void _option_init(void) {
    for (size_t i = 0; i < sizeof(_options) / sizeof(_options[0]); i++) {
        if (_options[i].type == PROTO_VALUE_STRING && _options[i].value.string) {
            free(_options[i].value.string);
            _options[i].value.string = NULL;
        }
    }
    sconf_load_with_proto(HRLOG_CONFIG_PATH, _options, sizeof(_options) / sizeof(_options[0]));

#if USE_HRLOG_PROTO_FILE
    _options[OPTION_PROTO].value.number = HRLOG_PROTO_FILE;
#endif

    // close rsyslog
    if (HRLOG_PROTO_RSYSLOG != _options[OPTION_PROTO].value.number) {
        
    }
    // printf("proto: %ld\n", _options[OPTION_PROTO].value.number);
    if (_options[OPTION_TAGS].value.string) {
        char* token = NULL;
        char* save_ptr = NULL;
        int i = 0;
        memset((void*)&_tags_filter, 0, sizeof(_tags_filter));
        for (token = strtok_r(_options[OPTION_TAGS].value.string, ",", &save_ptr);
             token;
             token = strtok_r(NULL, ",", &save_ptr)) {
            if (i > TAGS_SIZE - 1) {
                break;
            }
            strncpy(_tags_filter[i], token, TAGS_LENGTH - 1);
            i++;
        }
        // now we can free _options[OPTION_TAGS].value.string
        free(_options[OPTION_TAGS].value.string);
        _options[OPTION_TAGS].value.string = NULL;
    }

    for (int i = 0; i < TAGS_SIZE; i++) {
        if (_tags_filter[i][0] == '\0') {
            break;
        }
        printf("%d -> %s\n", i, _tags_filter[i]);
    }
}

static void on_option_changed(const char* path, void* priv) {
    (void)priv;
    if (!path)
        return;
    // printf("path:%s, %s\n", path, HRLOG_CONFIG_PATH);
    if (0 == strcmp(HRLOG_CONFIG_PATH, path)) {
        _option_init();

        // persist output maybe not enabled
        if (_options[OPTION_PROTO].value.number == HRLOG_PROTO_FILE) {
            if (!persist_fp) {
                init_persist_output();
            }
        }
    }
}
// 1-> allowed
// 0-> no
static int is_allowed(const char* tag) {
    if (!tag) return 0;

    for (int i = 0; i < TAGS_SIZE; i++) {
        if (_tags_filter[i][0] == '\0') {
            break;
        }
        if (0 == strcasecmp(tag, _tags_filter[i])) {
            return 1;
        }
    }
    return 0;
}

static void _init(void) {
    // openlog(NULL, LOG_PID, LOG_USER);
    const char* serial_field = "androidboot.serialno=";
    char cmdline[LINE_MAX] = {0};

    FILE* f = fopen("/proc/cmdline", "r");
    if (f) {
        if (NULL != fgets(cmdline, sizeof(cmdline), f)) {
            char* ptr = strstr(cmdline, "androidboot.serialno=");
            if (ptr) {
                char* end = strchr(ptr + strlen(serial_field), ' ');
                if (end) {
                    strncpy(_hostname, ptr + strlen(serial_field), end - (ptr + strlen(serial_field)));
                }
            }
        }
        fclose(f);
    }
    if (strlen(_hostname) == 0) {
        gethostname(_hostname, sizeof(_hostname));
    }

    _option_init();

    sconf_register_observer(HRLOG_CONFIG_PATH, on_option_changed, NULL);

    if (_options[OPTION_PROTO].value.number == HRLOG_PROTO_FILE) {
        init_persist_output();
    }
}

static int rsyslog(const char* message) {
    int offset = 0;
    char buffer[LINE_MAX] = {0};

    struct tm tm;
    struct timespec ts;

    if (_rsyslog.sock == -1) {
        int sock = -1;
        struct addrinfo hints, *res;
        memset(&hints, 0, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;

        if (getaddrinfo(RSYSLOG_SERVER, RSYSLOG_PORT, &hints, &res) != 0) {
            res_init();
            return -1;
        }

        sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if (sock < 0) {
            freeaddrinfo(res);
            return -1;
        }

        fcntl(sock, F_SETFL, fcntl(sock, F_GETFL) | O_NONBLOCK);

        _rsyslog.sock = sock;
        _rsyslog.res = res;
    }

    clock_gettime(CLOCK_REALTIME, &ts);
    (void)localtime_r(&ts.tv_sec, &tm);

    offset = snprintf(buffer, sizeof(buffer), "<28>");
    if (offset <= 0) {
        return -1;
    }

    // time
    size_t size = strftime(buffer + offset, sizeof(buffer) - offset, "%b %d %H:%M:%S", &tm);

    offset += size;

    snprintf(buffer + offset, sizeof(buffer) - offset, " %s %s", _hostname, message);

    int ret = sendto(_rsyslog.sock, buffer, strlen(buffer), 0,
                     (const struct sockaddr*)_rsyslog.res->ai_addr, _rsyslog.res->ai_addrlen);

    if (ret == -1) {
        printf("sock maybe broken : %d!!!!!!!", errno);
        syslog(LOG_SYSLOG, "sock maybe broken : %d!!!!!!!", errno);
        // switch (errno) {
        //     case ECONNRESET:
        //     case ENOTCONN:
        //     case EPIPE:
        close(_rsyslog.sock);
        _rsyslog.sock = -1;
        freeaddrinfo(_rsyslog.res);
        _rsyslog.res = NULL;
        // }
    }

    return 0;
}

int _hr_log_printf(int prio, const char* tag, const char* fmt, ...) {
    int ret = -1;
    (void)prio;

    va_list ap;
    char buf[LOG_BUF_SIZE] = {0};
    char* ptr = buf;
    size_t available = LOG_BUF_SIZE;
    struct tm tm;
    struct timespec ts;

    pthread_once(&persist_once_control, _init);

    if (prio < _options[OPTION_PRIORITY].value.number) {
        return 0;
    }

    if (!is_allowed(tag)) {
        // printf("not allow:%s\n", tag);
        return 0;
    }

    clock_gettime(CLOCK_REALTIME, &ts);
    (void)localtime_r(&ts.tv_sec, &tm);

    // time
    size_t size = strftime(ptr, available, "%m-%d %H:%M:%S", &tm);
    available -= size;
    ptr += size;
    // append ms
    ret = snprintf(ptr, available, ".%03ld ", ts.tv_nsec / 1000000);
    if (ret < 0)
        ret = 0;
    available -= (size_t)ret;
    ptr += ret;

    // pid & tid
    ret = snprintf(ptr, available, "%5d %5ld ", getpid(), syscall(SYS_gettid) /*gettid()*/);
    if (ret < 0)
        ret = 0;
    available -= (size_t)ret;
    ptr += ret;

    // tag
    if (tag) {
        ret = snprintf(ptr, available, "%-8s: ", tag);
        if (ret < 0)
            ret = 0;
        available -= (size_t)ret;
        ptr += ret;
    }

    va_start(ap, fmt);
    ret = vsnprintf(ptr, available, fmt, ap);
    // when it's too long, trunk and using ...
    if (ret > 0 && (size_t)ret > available) {
        // overflow
        buf[LOG_BUF_SIZE - 2] = '\n';
        buf[LOG_BUF_SIZE - 3] = '.';
        buf[LOG_BUF_SIZE - 4] = '.';
        buf[LOG_BUF_SIZE - 5] = '.';
    }
    va_end(ap);

    switch (_options[OPTION_PROTO].value.number) {
        case HRLOG_PROTO_FILE:
            if (persist_fp) {
                fprintf(persist_fp, "%s", buf);
            }
            break;
        case HRLOG_PROTO_SYSLOG:
            syslog(LOG_SYSLOG, "%s", buf);
            break;
        case HRLOG_PROTO_RSYSLOG:
            rsyslog(buf);
            break;
        default:
            printf("%s", buf);
            break;
    }
    return 0;
}
