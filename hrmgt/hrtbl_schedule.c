#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/inotify.h>

#include <sys/types.h>
#include <dirent.h>

#include "j2stable.h"
#include "hr_list.h"


#include "hrtbl_schedule.h"


#ifndef HR_LOGE
#define HR_LOGE printf
#endif

#ifndef J2STBL_BASE_DB_PATH
#define J2STBL_BASE_DB_PATH "./j2stbls"
#endif

#define J2STBL_TRIGGER_DIR "./lib_hrtbls"

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

struct hrtbl_schedule {
    struct hrtbl_schedule_module module;
    void *dlhandle;  // dlopen handle
    struct hr_list_head list;
};

struct hrtbl_schedule_priv {
    int epoll_fd;
    int running;
    struct hr_list_head schedules;
};

static struct hrtbl_schedule_priv _priv = {
    .epoll_fd = -1,
    .running = 0,
    .schedules = {&_priv.schedules, &_priv.schedules}};

struct hrtbl_schedule *_hrtbl_schedule_module_alloc(struct hrtbl_schedule_module *module) {
    struct hrtbl_schedule *s = NULL;
    if (!module) return NULL;

    s = (struct hrtbl_schedule *)calloc(1, sizeof(struct hrtbl_schedule));
    if (!s) return NULL;

    s->module = *module;
    HR_INIT_LIST_HEAD(&s->list);
    hr_list_add_tail(&s->list, &_priv.schedules);

    return s;
}

static int _load_module(const char *path) {
    DIR *dir = NULL;
    struct dirent *entry = NULL;
    struct stat st;

    if (!path) return -1;

    if (stat(path, &st) || !S_ISDIR(st.st_mode)) {
        HR_LOGE("it's not a good directory ...\n");
        return -1;
    }

    dir = opendir(path);
    if (!dir) {
        HR_LOGE("can not open directory ...\n");
        return -1;
    }

    while ((entry = readdir(dir))) {
        void *handle = NULL;
        char plugin_so[512] = {0};
        size_t len = 0;

        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;

        len = strlen(entry->d_name);

        if (len < 4 || entry->d_name[len - 1] != 'o' || entry->d_name[len - 2] != 's') continue;

        snprintf(plugin_so, sizeof(plugin_so), "%s/%s", path, entry->d_name);

        handle = dlopen(plugin_so, RTLD_NOW | RTLD_GLOBAL);
        if (!handle) {
            char *err = dlerror();
            HR_LOGE("load %s failed: %s\n", plugin_so, err);
        } else {
            HR_LOGE("load %s success!\n", plugin_so);
            struct hrtbl_schedule_module *sym = (struct hrtbl_schedule_module *)dlsym(handle, "HRTBL_TRIGGER_MODULE_SYM");
            if (!sym) {
                printf("invalid module:%s, ignored ...\n", entry->d_name);
                continue;
            }
            if (!sym->table || !sym->reload) {
                printf("invalid module ...\n");
                dlclose(handle);
                continue;
            }
            struct hrtbl_schedule *s = _hrtbl_schedule_module_alloc(sym);
            s->dlhandle = handle;
        }
    }
    
    closedir(dir);
    return 0;
}

static void _unload_module() {
    struct hrtbl_schedule *p = NULL, *n = NULL;
    hr_list_for_each_entry_safe(p, n, &_priv.schedules, list) {
        printf("module:%s\n", p->module.table);
        dlclose(p->dlhandle);
        _hr_list_del(&p->list);
        free(p);
    }
}
static void _inotify_j2stbl_monitor_task_routin(int fd) {
    const char *p;
    const struct inotify_event *e;
    char buf[4096] = {0};
    ssize_t size;

    struct hrtbl_schedule *schedule = NULL;

    size = TEMP_FAILURE_RETRY(read(fd, buf, sizeof(buf)));
    printf("read size:%ld\n", size);
    if (size == -1)
        return;

    /* Now we have one or more inotify_event structs. */
    for (p = buf; p < buf + size; p += sizeof(*e) + e->len) {
        e = (const struct inotify_event *)p;

        printf("mask:0x%X, len:%d, name:%s\n", e->mask, e->len, e->name);
        char* sub = strstr(e->name, ".json");
        if (!sub || sub[strlen(".json")] != '\0' ) {
          printf("not valid json:%s\n", e->name);
          _priv.running = 0;
          continue;
        }
        if (e->mask & (IN_ATTRIB | IN_MODIFY)) {
            printf("modify .....\n");
        }
        if (e->mask & ~(IN_ATTRIB | IN_MODIFY)) {
            printf("rename.....\n");
        }

        if (e->mask & IN_DELETE && e->len) {
            printf("The file '%s' was deleted. Recreating...\n", e->name);
        }

        hr_list_for_each_entry(schedule, &_priv.schedules, list) {
            printf("module:%s\n", schedule->module.table);
            if (0 == strncmp(e->name, schedule->module.table, strlen(e->name) - strlen(".json"))) {
                printf("found entry ... call it \n");
                schedule->module.reload(schedule->module.table);
            }
        }
    }
}

int main(int argc, const char **argv) {
    (void)argc;
    (void)argv;

    int fd = -1;
    struct epoll_event ev;

    _load_module(J2STBL_TRIGGER_DIR);

    _priv.epoll_fd = epoll_create1(O_CLOEXEC);
    if (_priv.epoll_fd < 0)
        return -1;

    fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (fd < 0) {
        close(_priv.epoll_fd);
        _priv.epoll_fd = -1;
        return -1;
    }
    int events = IN_ATTRIB | IN_CREATE | IN_MODIFY | IN_DELETE | IN_DELETE_SELF | IN_MOVE_SELF | IN_MOVED_FROM | IN_MOVED_TO;
    int wd = inotify_add_watch(fd, J2STBL_BASE_DB_PATH, events);
    if (wd < 0) {
        close(fd);
        close(_priv.epoll_fd);
        _priv.epoll_fd = -1;
        return -1;
    }
    
    printf("epoll:%d, fd:%d, J2STBL_BASE_DB_PATH:%s\n", _priv.epoll_fd, fd, J2STBL_BASE_DB_PATH);

    memset((void *)&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.fd = fd;
    epoll_ctl(_priv.epoll_fd, EPOLL_CTL_ADD, fd, &ev);

    _priv.running = 1;

    while (_priv.running != 0) {
        int epoll_loop_break = 0;
        struct epoll_event evs[24];

        for (;;) {
            int timeout = -1;  // 12000;

            int nr = TEMP_FAILURE_RETRY(epoll_wait(_priv.epoll_fd, evs, ARRAY_SIZE(evs), timeout));
            printf("nr:%d\n", nr);
            if (nr == 0 || nr == -1) {
                // timeout
                printf("maybe timeout ...\n");
                epoll_loop_break = 1;
                break;
            }

            if (epoll_loop_break) break;

            for (int i = 0; i < nr; i++) {
                struct epoll_event *e = evs + i;
                _inotify_j2stbl_monitor_task_routin(e->data.fd);
            }
            
            if (_priv.running == 0) break;
        }
    }

    inotify_rm_watch(fd, wd);
    fd = -1;
    close(_priv.epoll_fd);
    _priv.epoll_fd = -1;

    _unload_module();

    return 0;
}
