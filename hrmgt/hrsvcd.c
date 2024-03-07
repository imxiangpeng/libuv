#define _GNU_SOURCE
#include <assert.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "j2sobject.h"

#define MAX_ARGS 64

#ifndef HRSVC_CONFIG_DIR
#define HRSVC_CONFIG_DIR "init.d"
#endif

#ifndef HRSVC_UNIX_SOCK
#define HRSVC_UNIX_SOCK "./var/svcd.sock"
#endif

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

#define IPC_MESSAGE_CMD_HEAD 0x12340000

#define HRINIT_MESSAGE_BASE 0x12340000
#define HRINIT_MESSAGE_SERVICE 0x12340000
#define HRINIT_MESSAGE_SERVICE_START (HRINIT_MESSAGE_SERVICE | 0x01)
#define HRINIT_MESSAGE_SERVICE_STOP (HRINIT_MESSAGE_SERVICE | 0x02)
#define HRINIT_MESSAGE_SERVICE_STATUS (HRINIT_MESSAGE_SERVICE | 0x03)
#define IPC_MESSAGE_CMD_ACK 0x12347890

#define HRSVC_DISABLED 0x01   /* do not autostart with class */
#define HRSVC_ONESHOT 0x02    /* do not restart on exit */
#define HRSVC_RUNNING 0x04    /* currently active */
#define HRSVC_RESTARTING 0x08 /* waiting to restart */

struct hrsvcd_priv {
    int epoll_fd;
    int svc_fd;
    struct {
        int write_fd;
        int recv_fd;
    } signal;
    struct j2sobject *svcs;
};
// static struct j2sobject *_priv.svcs = NULL;
static struct hrsvcd_priv _priv = {-1, -1, {-1, -1}, NULL};

// 获取元素 ele 在结构体中偏移量
#define _J2SOBJECT_SERVICE_DATA_OFFSET(ele) \
    offsetof(struct hrsvc, ele)

// 获取结构体中 ele 元素大小
#define _J2SOBJECT_SERVICE_DATA_LEN(ele) \
    sizeof(((struct hrsvc *)0)->ele)

#define _J2SOBJECT_SERVICE_DATA_ARRAY_LEN(ele) \
    sizeof(((struct hrsvc *)0)->ele) / sizeof(((struct hrsvc *)0)->ele[0])

typedef struct hrsvc {
    J2SOBJECT_DECLARE_OBJECT;
    char name[24];   // service name
    char class[24];  // service class boot at which stage
    int disabled;    // auto startup
    int oneshot;     // auto startup
    // action list
    // int nargs;
    char *argv[MAX_ARGS];  // max args
    int flags;             // append other not stored fields
    pid_t pid;
    time_t time_started;
} hrsvc_t;

static int hrsvc_ctor(struct j2sobject *obj);

static struct j2sobject_prototype _hrsvc_prototype = {
    .name = "service",
    .type = J2S_OBJECT,
    .size = sizeof(struct hrsvc),
    .ctor = hrsvc_ctor,
    .dtor = NULL};

static struct j2sobject_fields_prototype _hrsvc_fields_prototype[] = {
    {.name = "name", .type = J2S_STRING, .offset = _J2SOBJECT_SERVICE_DATA_OFFSET(name), .offset_len = _J2SOBJECT_SERVICE_DATA_ARRAY_LEN(name)},
    {.name = "class", .type = J2S_STRING, .offset = _J2SOBJECT_SERVICE_DATA_OFFSET(class), .offset_len = _J2SOBJECT_SERVICE_DATA_ARRAY_LEN(class)},
    {.name = "disabled", .type = J2S_INT, .offset = _J2SOBJECT_SERVICE_DATA_OFFSET(disabled), .offset_len = 0},
    {.name = "oneshot", .type = J2S_INT, .offset = _J2SOBJECT_SERVICE_DATA_OFFSET(oneshot), .offset_len = 0},
    {.name = "argv", .type = J2S_ARRAY | J2S_STRING, .offset = _J2SOBJECT_SERVICE_DATA_OFFSET(argv), .offset_len = _J2SOBJECT_SERVICE_DATA_ARRAY_LEN(argv) /*string buffer will dynamic allocated when needed*/},
    // flag not stored
    {0}};

static int hrsvc_ctor(struct j2sobject *obj) {
    if (!obj)
        return -1;
    obj->name = "service";
    obj->field_protos = _hrsvc_fields_prototype;
    return 0;
}

static int hrsvc_load_service(const char *directory) {
    DIR *dir = NULL;
    struct dirent *entry = NULL;
    struct stat st;

    if (!directory) return -1;

    if (stat(directory, &st) || !S_ISDIR(st.st_mode)) {
        printf("it's not a good directory ...\n");
        return -1;
    }

    dir = opendir(directory);
    if (!dir) {
        printf("can not open directory ...\n");
        return -1;
    }

    while ((entry = readdir(dir))) {
        char path[512] = {0};
        size_t len = 0;

        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;

        len = strlen(entry->d_name);

        snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);

        struct hrsvc *svc = (struct hrsvc *)j2sobject_create(&_hrsvc_prototype);

        int ret = j2sobject_deserialize_file(J2SOBJECT(svc), path);
        if (ret != 0) {
            printf("parse %s failed\n", entry->d_name);
            j2sobject_free(J2SOBJECT(svc));
            continue;
        }

        if (svc->disabled != 0) {
            svc->flags |= HRSVC_DISABLED;
        }

        if (svc->oneshot != 0) {
            svc->flags |= HRSVC_ONESHOT;
        }

        printf("+++++++++++++++++++++++++++\n");
        printf("name: %s\n", svc->name);
        printf("class: %s\n", svc->class);
        printf("disabled : %d\n", svc->disabled);
        printf("oneshot: %d\n", svc->oneshot);
        printf("argv:");
        for (unsigned i = 0; i < ARRAY_SIZE(svc->argv); i++) {
            if (!svc->argv[i]) break;
            printf(" %s", svc->argv[i]);
        }
        printf("\n");
        printf("+++++++++++++++++++++++++++\n");
        // insert into tail
        _priv.svcs->prev->next = J2SOBJECT(svc);
        J2SOBJECT(svc)->prev = _priv.svcs->prev;
        J2SOBJECT(svc)->next = _priv.svcs;
        _priv.svcs->prev = J2SOBJECT(svc);
    }

    closedir(dir);
    return 0;
}

/*
 * gettime() - returns the time in seconds of the system's monotonic clock or
 * zero on error.
 */
static time_t _gettime(void) {
    struct timespec ts;
    int ret;

    ret = clock_gettime(CLOCK_MONOTONIC, &ts);
    if (ret < 0) {
        printf("clock_gettime(CLOCK_MONOTONIC) failed: %s\n", strerror(errno));
        return 0;
    }

    return ts.tv_sec;
}

static int _hrsvcd_create_socket() {
    int ret = -1;
    int type = SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK;
    struct sockaddr_un addr;

    int fd = socket(AF_UNIX, type, 0);
    if (fd < 0) return -1;

    fcntl(fd, F_SETFD, FD_CLOEXEC);
    fcntl(fd, F_SETFL, O_NONBLOCK);

    memset((void *)&addr, 0, sizeof(addr));

    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", HRSVC_UNIX_SOCK);

    if ((unlink(addr.sun_path) != 0) && (errno != ENOENT)) {
        printf("can not unlink old file\n");
        return -1;
    }

    ret = bind(fd, (struct sockaddr *)&addr, sizeof(addr));
    if (ret != 0) {
        unlink(addr.sun_path);
        close(fd);
        printf("call bind error :%s ...\n", strerror(errno));
        return -1;
    }

    listen(fd, 8);
    return fd;
}

static int _recv_fully(int fd, void *data_ptr, size_t size) {
    size_t bytes_left = size;
    char *data = (char *)data_ptr;

    if (fd < 0) return -1;
    while (bytes_left > 0) {
        int result =
            TEMP_FAILURE_RETRY(recv(fd, data, bytes_left, MSG_DONTWAIT));
        if (result <= 0) {
            return -1;
        }

        bytes_left -= result;
        data += result;
    }

    return bytes_left == 0 ? 0 : -1;
}

static char *_recv_string(int fd) {
    int ret = 0;
    int len = 0;
    void *ptr = NULL;
    ret = _recv_fully(fd, (void *)&len, sizeof(len));
    if (ret != 0) {
        // printf("read data error ...size.\n");
        return NULL;
    }

    ptr = calloc(1, len + 1);  // '\0'
    if (!ptr) {
        // can not alloc memory, but we should read all data
        int i = 0;
        for (i = 0; i < len; i++) {
            char data = 0;
            _recv_fully(fd, (void *)&data, sizeof(data));
        }
        // TEMP_FAILURE_RETRY(send(fd, &reply, sizeof(reply), 0));
        return NULL;
    }

    // data
    ret = _recv_fully(fd, (void *)ptr, len);

    return ptr;
}

static struct hrsvc *_hrsvcd_find_by_name(const char *name) {
    struct j2sobject *n = NULL;
    struct hrsvc *svc = NULL;

    if (!name) return NULL;

    for (n = _priv.svcs->next; n != _priv.svcs; n = n->next) {
        svc = (struct hrsvc *)n;
        if (0 == strcmp(svc->name, name)) {
            return svc;
        }
    }

    return NULL;
}
static struct hrsvc *_hrsvcd_find_by_pid(pid_t pid) {
    struct j2sobject *n = NULL;
    struct hrsvc *svc = NULL;

    for (n = _priv.svcs->next; n != _priv.svcs; n = n->next) {
        svc = (struct hrsvc *)n;
        if (pid == svc->pid) {
            return svc;
        }
    }

    return NULL;
}

static void _hrsvcd_service_start(struct hrsvc *svc) {
    pid_t pid = -1;

    if (!svc) return;

    printf("try to start :%s\n", svc->name);

    if (svc->flags & HRSVC_RUNNING) {
        return;
    }

    svc->time_started = 0;

    pid = fork();
    if (pid == 0) {  // child pid
        // service process
        if (execve(svc->argv[0], (char **)svc->argv, (char **)NULL) < 0) {
            printf("can not start process:%s\n", svc->name);
        }
        fprintf(stderr, "child %s finished\n", svc->name);
        exit(127);
    }

    if (pid < 0) {
        printf("can not start service:%s\n", svc->name);
        return;
    }

    svc->pid = pid;
    svc->flags |= HRSVC_RUNNING;
    svc->time_started = _gettime();
    printf("start :%s success :%d\n", svc->name, pid);
    // service started
}

static void _hrsvcd_service_stop(struct hrsvc *svc) {
    if (!svc) return;
    printf("try to kill %s (%d)\n", svc->name, svc->pid);
    if (svc->pid == 0) return;
    if (svc->flags & HRSVC_RUNNING) {
        printf("kill %s (%d)\n", svc->name, svc->pid);
        kill(svc->pid, SIGKILL);
        kill(-svc->pid, SIGKILL);
        // running will be remove in child signal handle
    }
}
// hr_attribute
static int _hrsvcd_stream_available(int fd) {
    int op = 0;
    int len = 0;
    char *name = NULL;
    char *response = NULL;
    int response_size = 0;

    if (fd < 0) {
        printf("_data_available: invalid fd:%d\n", fd);
        return -1;
    }

    int ret = _recv_fully(fd, (void *)&op, sizeof(op));
    if (ret != 0) {
        // printf("read data error ...cmd .\n");
        return -1;
    }

    printf("op:0x%X\n", op);
    if ((op & 0xFFFF0000) != IPC_MESSAGE_CMD_HEAD) {
        printf("invalid header ignore ...\n");
        return -1;
    }

    switch (op) {
        case HRINIT_MESSAGE_SERVICE_START: {
            struct hrsvc *svc = NULL;
            name = _recv_string(fd);
            if (!name) return -1;
            svc = _hrsvcd_find_by_name(name);
            free(name);
            name = NULL;
            _hrsvcd_service_start(svc);
            break;
        }

        case HRINIT_MESSAGE_SERVICE_STOP: {
            struct hrsvc *svc = NULL;
            name = _recv_string(fd);
            if (!name) return -1;

            svc = _hrsvcd_find_by_name(name);
            free(name);
            name = NULL;

            _hrsvcd_service_stop(svc);
            break;
        }
        case HRINIT_MESSAGE_SERVICE_STATUS: {
            struct hrsvc *svc = NULL;
            name = _recv_string(fd);
            if (!name) return -1;

            svc = _hrsvcd_find_by_name(name);
            free(name);
            name = NULL;

            response_size = asprintf(&response, "name:%s\npid:%d\nstatus:%d\n", svc->name, svc->pid, (svc->flags & HRSVC_RUNNING) == HRSVC_RUNNING);

            // printf("service %s(pid:%d) status:0x%x\n", svc->name, svc->pid, svc->flags & HRSVC_RUNNING);
            break;
        }
        default:
            break;
    }

    if (name)
        free(name);

    printf("response size:%d -> %s\n", response_size, response);
    // send ack
    TEMP_FAILURE_RETRY(send(fd, &response_size, sizeof(response_size), 0));
    if (response) {
        TEMP_FAILURE_RETRY(send(fd, response, response_size, 0));
        free(response);
    }

    return 0;
}

static void _hrsvc_sigchld_handler(int s) {
    // notify main loop that child finished
    write(_priv.signal.write_fd, &s, 1);
}

static int _hrsvc_wait_for_one_process(int block) {
    pid_t pid;
    int status;

    while ((pid = waitpid(-1, &status, block ? 0 : WNOHANG)) == -1 && errno == EINTR)
        ;
    if (pid <= 0) return -1;

    printf("waitpid returned pid %d, status = %08x\n", pid, status);

    struct hrsvc *svc = _hrsvcd_find_by_pid(pid);
    if (!svc) return 0;

    printf("%s(%d): service %s(%d) stopped\n", __FUNCTION__, __LINE__, svc->name, svc->pid);

    if (!(svc->flags & HRSVC_ONESHOT)) {
        kill(-pid, SIGKILL);
        printf("process '%s' killing any children in process group\n", svc->name);
    }
    svc->pid = 0;
    svc->flags &= (~HRSVC_RUNNING);

    if (!(svc->flags & HRSVC_ONESHOT)) {
        printf("we should restart ...\n");
        _hrsvcd_service_start(svc);
    }
    return 0;
}
static void _hrsvc_handle_signal(int fd) {
    char tmp[32] = {0};

    if (fd < 0) return;

    /* we got a SIGCHLD - reap and restart as needed */
    read(fd, tmp, sizeof(tmp));
    while (!_hrsvc_wait_for_one_process(0))
        ;
}
static void signal_init(void) {
    int s[2] = {-1};

    struct sigaction act;
    memset(&act, 0, sizeof(act));
    act.sa_handler = _hrsvc_sigchld_handler;
    act.sa_flags = SA_NOCLDSTOP;
    sigaction(SIGCHLD, &act, 0);

    /* create a signalling mechanism for the sigchld handler */
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, s) == 0) {
        _priv.signal.write_fd = s[0];
        _priv.signal.recv_fd = s[1];
        fcntl(s[0], F_SETFD, FD_CLOEXEC);
        fcntl(s[0], F_SETFL, O_NONBLOCK);
        fcntl(s[1], F_SETFD, FD_CLOEXEC);
        fcntl(s[1], F_SETFL, O_NONBLOCK);
    }

    _hrsvc_handle_signal(_priv.signal.recv_fd);

    return;
}
// hrsvcd
int main(int argc, const char **argv) {
    (void)argc;
    (void)argv;
    struct epoll_event ev;
    struct epoll_event evs[24];

    _priv.epoll_fd = epoll_create1(O_CLOEXEC);
    if (_priv.epoll_fd < 0)
        return -1;

    _priv.svc_fd = _hrsvcd_create_socket();
    if (_priv.svc_fd < 0) {
        close(_priv.epoll_fd);
        return -1;
    }
    signal_init();
    _priv.svcs = j2sobject_create_array(&_hrsvc_prototype);
    assert(_priv.svcs);

    hrsvc_load_service(HRSVC_CONFIG_DIR);

    memset((void *)&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.fd = _priv.svc_fd;
    epoll_ctl(_priv.epoll_fd, EPOLL_CTL_ADD, _priv.svc_fd, &ev);

    memset((void *)&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.fd = _priv.signal.recv_fd;
    epoll_ctl(_priv.epoll_fd, EPOLL_CTL_ADD, _priv.signal.recv_fd, &ev);

    for (;;) {
        int timeout = -1;  // 12000;

        int nr = TEMP_FAILURE_RETRY(epoll_wait(_priv.epoll_fd, evs, ARRAY_SIZE(evs), timeout));
        if (nr == 0 || nr == -1) {
            // timeout
            printf("maybe timeout ...\n");
            continue;
        }

        for (int i = 0; i < nr; i++) {
            struct epoll_event *e = evs + i;
            // new connection ?
            if (_priv.svc_fd == e->data.fd) {
                int fd = accept4(e->data.fd, NULL, NULL, SOCK_CLOEXEC);
                if (fd < 0) {
                    continue;
                }

                memset((void *)&ev, 0, sizeof(ev));
                ev.events = EPOLLIN;
                ev.data.fd = fd;
                if (epoll_ctl(_priv.epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1) {
                    close(fd);
                    continue;
                }
            } else if (_priv.signal.recv_fd == e->data.fd) {
                _hrsvc_handle_signal(e->data.fd);
            } else {  // service stream
                // we should read stream
                int ret = _hrsvcd_stream_available(e->data.fd);
                if (ret < 0) {
                    // we should remove fd from epoll
                    epoll_ctl(_priv.epoll_fd, EPOLL_CTL_DEL, e->data.fd, NULL);
                    close(e->data.fd);
                }
            }
        }
    }

    // never enter here

    printf("stop ...\n");
    // all elements on the link will be freed
    j2sobject_free(_priv.svcs);
    _priv.svcs = NULL;

    // monitored fd ignored, because it will be auto released in loop
    close(_priv.epoll_fd);
    _priv.epoll_fd = -1;

    close(_priv.signal.recv_fd);
    close(_priv.signal.write_fd);
    _priv.signal.write_fd = -1;
    _priv.signal.recv_fd = -1;

    return 0;
}
