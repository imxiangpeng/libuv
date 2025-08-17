// mxp, 20250520, smart/simple conf utils
// support:
// KEY=VALUE
// KEY VALUE
// KEY = VALUE

#define _GNU_SOURCE

#include "sconf.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>

#include "file_util.h"
#include "hr_list.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

#ifndef TEMP_FAILURE_RETRY
#define TEMP_FAILURE_RETRY(exp)                \
    ({                                         \
        typeof(exp) _rc;                       \
        do {                                   \
            _rc = (exp);                       \
        } while (_rc == -1 && errno == EINTR); \
        _rc;                                   \
    })
#endif

struct observer {
    char dir[256];
    char name[256];
    void* priv;
    sconf_observer observer;
    struct hr_list_head entry;
};

struct watcher {
    int wd;
    char dir[256];
    struct hr_list_head entry;
};

static pthread_once_t _once_init = PTHREAD_ONCE_INIT;
static pthread_t _observer_tid = 0;
static pthread_mutex_t _queue_lock;

static HR_LIST_HEAD(_sconf_observer_queue);

static HR_LIST_HEAD(_inotify_watcher_queue);

static int epoll_fd = -1;
static int event_fd = -1;
static int inotify_fd = -1;

static void* _sconf_auto_reload_task(void* args) {
    (void)args;
    int running = 1;
    while (running != 0) {
        int epoll_loop_break = 0;
        struct epoll_event evs[24];

        for (;;) {
            int timeout = -1;  // 12000;

            int nr = TEMP_FAILURE_RETRY(epoll_wait(epoll_fd, evs, ARRAY_SIZE(evs), timeout));
            if (nr == 0 || nr == -1) {
                // timeout
                // printf("maybe timeout ...\n");
                epoll_loop_break = 1;
                break;
            }

            if (epoll_loop_break) break;

            for (int i = 0; i < nr; i++) {
                struct epoll_event* e = evs + i;

                if (e->data.fd == event_fd) {
                    // receive event, should we
                    int msg = 0;
                    read(e->data.fd, &msg, sizeof(msg));
                    continue;
                }

                if (e->data.fd == inotify_fd) {
                    const char* p;
                    const struct inotify_event* ie;
                    char buf[4096] = {0};
                    size_t size = TEMP_FAILURE_RETRY(read(inotify_fd, buf, sizeof(buf)));

                    /* Now we have one or more inotify_event structs. */
                    for (p = buf; p < buf + size; p += sizeof(*ie) + ie->len) {
                        ie = (const struct inotify_event*)p;

                        printf("mask:0x%X, len:%d, name:%s, wd:%d\n", ie->mask, ie->len, ie->name, ie->wd);
                        if (ie->mask & (IN_ATTRIB | IN_MODIFY)) {
                            printf("modify .....\n");
                        }
                        if (ie->mask & ~(IN_ATTRIB | IN_MODIFY)) {
                            printf("rename.....\n");
                        }

                        if (ie->mask & IN_DELETE && ie->len) {
                            printf("The file '%s' was deleted. Recreating...\n", ie->name);
                        }
                        if ((ie->mask & IN_MOVED_TO) || ie->mask & (IN_ATTRIB | IN_MODIFY)) {
                            // printf("The file '%s' was moved to, or modified.\n", ie->name);

                            pthread_mutex_lock(&_queue_lock);
                            if (!hr_list_empty(&_inotify_watcher_queue)) {
                                struct watcher* w = NULL;
                                hr_list_for_each_entry(w, &_inotify_watcher_queue, entry) {
                                    if (ie->wd == w->wd) {
                                        if (!hr_list_empty(&_sconf_observer_queue)) {
                                            struct observer* e = NULL;
                                            hr_list_for_each_entry(e, &_sconf_observer_queue, entry) {
                                                // printf("check watcher dir:%s vs %s...cmp:%d, name:%s %d\n", e->dir, w->dir, strcmp(e->dir, w->dir), e->name, strcmp(e->name, ie->name));
                                                // exist!
                                                if (!strcmp(e->dir, w->dir) && !strcmp(e->name, ie->name) && e->observer != NULL) {
                                                    char path[512] = {0};
                                                    snprintf(path, sizeof(path), "%s/%s", e->dir, e->name);
                                                    // printf("fire for :%s\n", path);
                                                    e->observer(path, e->priv);
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                            pthread_mutex_unlock(&_queue_lock);
                        }
                    }
                }
            }

            if (running == 0) break;
        }
    }

    // inotify_rm_watch && close

    return NULL;
}

static void _sconf_init(void) {
    pthread_attr_t attr;

    pthread_mutex_init(&_queue_lock, NULL);

    epoll_fd = epoll_create1(O_CLOEXEC);
    if (epoll_fd < 0) {
        return;
    }

    event_fd = eventfd(0, EFD_NONBLOCK);
    if (event_fd < 0) {
        close(epoll_fd);
        return;
    }

    struct epoll_event ev = {
        .events = EPOLLIN,
        .data.fd = event_fd,
    };

    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, event_fd, &ev);

    inotify_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (inotify_fd < 0) {
        close(epoll_fd);
        epoll_fd = -1;
        close(event_fd);
        event_fd = -1;
        return;
    }

    ev.data.fd = inotify_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, inotify_fd, &ev);

    pthread_attr_init(&attr);

    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&_observer_tid, &attr, _sconf_auto_reload_task, NULL);
}
static int parse_conf_line(char* line, const char** fields, int64_t* result, size_t max) {
    int64_t val;
    char* p = line;
    char* save_ptr;
    char* endptr;
    int idx = -1;

    while (*line && isspace((unsigned char)*line))
        ++line;

    if (*line == '#' || *line == '\0') {
        return 0;
    }

    p = strtok_r(line, " =", &save_ptr);
    if (!p) {
        return -1;
    }

    for (size_t i = 0; i < max; i++) {
        if (0 == strcmp(fields[i], p)) {
            idx = i;
        }
    }

    if (idx < 0) {
        // no need parse, it's not error
        return 0;
    }

    p = strtok_r(NULL, " =", &save_ptr);
    if (!p) {
        return -1;
    }

    val = strtoll(p, &endptr, 10);
    if (p == endptr || val > INT64_MAX) {
        return -1;
    }
    result[idx] = val;

    return 0;
}

int sconf_load_int64(const char* path, const char** fields, int64_t* result, size_t max) {
    ssize_t len = 0;
    char* buf = NULL;
    char* line = NULL;
    char* save_ptr = NULL;
    if (!path || !fields || !result) {
        return -1;
    }

    len = futil_read(path, &buf);
    if (len <= 0 || !buf) {
        return -1;
    }

    for (line = strtok_r(buf, "\n", &save_ptr); line;
         line = strtok_r(NULL, "\n", &save_ptr)) {
        if (0 != parse_conf_line(line, fields, result, max)) {
            free(buf);
            return -1;
        }
    }

    free(buf);
    return 0;
}

int sconf_save_int64(const char* path, const char** fields, int64_t* result, size_t max) {
    int fd = -1;
    char* tmp = NULL;
    int tmp_len = 0;

    char buffer[1024] = {0};

    if (!path || !fields || !result) {
        return -1;
    }
    const char* TMPFILE_TEMPLATE = "tmp_XXXXXX";

    tmp_len = strlen(path) + strlen(TMPFILE_TEMPLATE) + 1;  // + '\0'

    tmp = (char*)calloc(1, tmp_len);  // hardcode 8(.XXXXXX + \0)
    if (!tmp)
        return -1;

    snprintf(tmp, tmp_len, "%s%s", path, TMPFILE_TEMPLATE);
    fd = mkostemp(tmp, O_RDWR | O_TRUNC | O_CREAT);
    if (fd < 0) {
        free(tmp);
        return -1;
    }

    fchmod(fd, S_IRUSR | S_IWUSR | S_IRGRP);

    for (size_t i = 0; i < max; i++) {
        int len = snprintf(buffer, sizeof(buffer), "%s=%ld\n", fields[i], result[i]);
        ssize_t r = futil_write_fd(fd, buffer, len);
        if (r != len) {
            close(fd);
            unlink(tmp);
            free(tmp);
            return -1;
        }
    }
    fdatasync(fd);
    close(fd);

    unlink(path);
    rename(tmp, path);
    free(tmp);
    return 0;
}

static int parse_conf_line_with_proto(char* line, struct sconf_proto* proto, size_t size) {
    char* p = line;
    char* save_ptr;
    char* endptr;
    int idx = -1;
    if (!line || !proto) {
        return -1;
    }

    while (*line && isspace((unsigned char)*line))
        ++line;

    if (*line == '#' || *line == '\0') {
        return 0;
    }

    p = strtok_r(line, " =", &save_ptr);
    if (!p) {
        return -1;
    }

    for (size_t i = 0; i < size; i++) {
        if (!proto[i].name) continue;
        if (0 == strcmp(proto[i].name, p)) {
            idx = i;
        }
    }

    if (idx < 0) {
        // no need parse, it's not error
        return 0;
    }

    p = strtok_r(NULL, " =", &save_ptr);
    if (!p) {
        // do not return error which cause losing all left data
        if (PROTO_VALUE_STRING == proto[idx].type) {
            proto[idx].value.string = strdup("");
        }
        return 0;
    }

    switch (proto[idx].type) {
        case PROTO_VALUE_NUMBER: {
            int64_t val = strtoll(p, &endptr, 10);
            if (p == endptr || val > INT64_MAX) {
                return -1;
            }
            proto[idx].value.number = val;
            return 0;
        }
        case PROTO_VALUE_DECIMAL: {
            double val = strtod(p, &endptr);
            if (p == endptr || errno == ERANGE) {
                return -1;
            }
            proto[idx].value.decimal = val;
            return 0;
        }

        case PROTO_VALUE_STRING:
            // do not free other memory
            // so you can pre assign string value as default value
            /*if (proto[idx].value.string != NULL) {
                free(proto[idx].value.string);
                proto[idx].value.string = NULL;
            }*/

            proto[idx].value.string = strdup(p);
            return 0;
        default:
            return -1;
    }
    return -1;
}

int sconf_load_with_proto(const char* path, struct sconf_proto* proto, size_t size) {
    ssize_t len = 0;
    char* buf = NULL;
    char* line = NULL;
    char* save_ptr = NULL;
    if (!path || !proto) {
        return -1;
    }

    len = futil_read(path, &buf);
    if (len <= 0 || !buf) {
        return -1;
    }

    for (line = strtok_r(buf, "\n", &save_ptr); line;
         line = strtok_r(NULL, "\n", &save_ptr)) {
        if (0 != parse_conf_line_with_proto(line, proto, size)) {
            free(buf);
            return -1;
        }
    }

    free(buf);
    return 0;
}

// 0: not match
// 1: matched
static int match_field(char* line, struct sconf_proto* proto, size_t size) {
    char* p = NULL;
    if (!line || !proto) {
        return 0;
    }

    while (*line && isspace((unsigned char)*line))
        ++line;

    if (*line == '#' || *line == '\0') {
        return 0;
    }

    p = line;

    // find space or =
    while (*p && *p != '=' && !isspace(*p))
        p++;

    for (size_t i = 0; i < size; i++) {
        if (!proto[i].name) continue;
        if (0 == strncmp(proto[i].name, line, p - line)) {
            return 1;
        }
    }

    return 0;
}

int sconf_save_with_proto(const char* path, struct sconf_proto* proto, size_t size) {
    int fd = -1;
    char* tmp = NULL;
    int tmp_len = 0;

    ssize_t len = 0;
    char buffer[1024] = {0};

    char* buf = NULL;
    char* line = NULL;
    char* save_ptr = NULL;

    if (!path || !proto) {
        return -1;
    }

    const char* TMPFILE_TEMPLATE = "tmp_XXXXXX";

    tmp_len = strlen(path) + strlen(TMPFILE_TEMPLATE) + 1;  // + '\0'

    tmp = (char*)calloc(1, tmp_len);  // hardcode 8(.XXXXXX + \0)
    if (!tmp)
        return -1;

    snprintf(tmp, tmp_len, "%s%s", path, TMPFILE_TEMPLATE);
    fd = mkostemp(tmp, O_RDWR | O_TRUNC | O_CREAT);
    if (fd < 0) {
        free(tmp);
        return -1;
    }

    fchmod(fd, S_IRUSR | S_IWUSR | S_IRGRP);

    len = futil_read(path, &buf);
    if (len > 0 && buf) {
        for (line = strtok_r(buf, "\n", &save_ptr); line;
             line = strtok_r(NULL, "\n", &save_ptr)) {
            if (0 == match_field(line, proto, size)) {
                // directly write origin data
                futil_write_fd(fd, line, strlen(line));
                futil_write_fd(fd, "\n", 1);  // append line eof
            }
        }

        free(buf);
    }

    for (size_t i = 0; i < size; i++) {
        if (!proto[i].name) continue;
        switch (proto[i].type) {
            case PROTO_VALUE_NUMBER: {
                int len = snprintf(buffer, sizeof(buffer), "%s=%ld\n", proto[i].name, proto[i].value.number);
                futil_write_fd(fd, buffer, len);
                break;
            }
            case PROTO_VALUE_DECIMAL: {
                int len = snprintf(buffer, sizeof(buffer), "%s=%f\n", proto[i].name, proto[i].value.decimal);
                futil_write_fd(fd, buffer, len);
                break;
            }
            case PROTO_VALUE_STRING: {
                // when data is null it will be deleted
                if (proto[i].value.string) {
                    futil_write_fd(fd, (void*)proto[i].name, strlen(proto[i].name));
                    futil_write_fd(fd, "=", 1);  // append line eof
                    futil_write_fd(fd, (void*)proto[i].value.string, strlen(proto[i].value.string));
                    futil_write_fd(fd, "\n", 1);  // append line eof
                }
                break;
            }
            default:
                break;
        }
    }

    fdatasync(fd);
    close(fd);

    unlink(path);
    rename(tmp, path);
    free(tmp);

    return 0;
}

int sconf_register_observer(const char* path, sconf_observer observer, void* priv) {
    struct observer* e = NULL;
    char* dir = NULL;
    char* name = NULL;

    char* tmp = NULL;

    int exist = 0;

    if (!path || !observer) {
        return -1;
    }

    pthread_once(&_once_init, _sconf_init);

    if (epoll_fd < 0 || event_fd < 0) {
        return -1;
    }

    tmp = strdup(path);
    if (!tmp) {
        return -1;
    }
    char* last_slash = strrchr(tmp, '/');
    name = last_slash ? last_slash + 1 : tmp;
    if (!last_slash) {
        dir = ".";
    } else {
        *last_slash = '\0';
        dir = tmp;
    }

    pthread_mutex_lock(&_queue_lock);
    if (!hr_list_empty(&_sconf_observer_queue)) {
        hr_list_for_each_entry(e, &_sconf_observer_queue, entry) {
            // exist!
            if (!strcmp(e->dir, path) && !strcmp(e->name, name) && e->observer == observer) {
                pthread_mutex_unlock(&_queue_lock);
                free(tmp);
                return -1;
            }
        }
    }

    // memory will not be freed
    e = (struct observer*)calloc(1, sizeof(struct observer));
    if (!e) {
        pthread_mutex_unlock(&_queue_lock);
        free(tmp);
        return -1;
    }

    HR_INIT_LIST_HEAD(&e->entry);
    strcpy(e->dir, dir);
    strcpy(e->name, name);
    e->observer = observer;
    e->priv = priv;

    hr_list_add_tail(&e->entry, &_sconf_observer_queue);

    if (!hr_list_empty(&_inotify_watcher_queue)) {
        struct watcher* w = NULL;
        hr_list_for_each_entry(w, &_inotify_watcher_queue, entry) {
            // exist!
            if (!strcmp(w->dir, dir)) {
                exist = 1;
                break;
            }
        }
    }

    if (exist == 0) {
        int events = IN_ATTRIB | IN_CREATE | IN_MODIFY | IN_DELETE | IN_DELETE_SELF | IN_MOVE_SELF | IN_MOVED_FROM | IN_MOVED_TO;
        struct watcher* w = (struct watcher*)calloc(1, sizeof(struct watcher));
        if (!w) {
            hr_list_del(&e->entry);
            pthread_mutex_unlock(&_queue_lock);
            free(e);
            free(tmp);
            return -1;
        }

        HR_INIT_LIST_HEAD(&w->entry);
        strncpy(w->dir, dir, sizeof(w->dir));
        w->wd = inotify_add_watch(inotify_fd, w->dir, events);
        if (w->wd < 0) {
            hr_list_del(&e->entry);

            free(e);
            free(w);

            pthread_mutex_unlock(&_queue_lock);
            free(tmp);
            return -1;
        }

        hr_list_add_tail(&w->entry, &_inotify_watcher_queue);

        int msg = 1;
        write(event_fd, &msg, sizeof(msg));
    }

    pthread_mutex_unlock(&_queue_lock);

    free(tmp);
    return 0;
}