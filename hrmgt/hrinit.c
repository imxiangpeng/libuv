#include <sys/wait.h>
#include <time.h>
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
#include <unistd.h>

#include "hr_list.h"
#include "hrtbl_schedule.h"
#include "j2sobject.h"

#define MAX_ARGS 64

#ifndef HR_LOGE
#define HR_LOGE printf
#endif

#ifndef J2STBL_BASE_DB_PATH
#define J2STBL_BASE_DB_PATH "./j2stbls"
#endif

#define J2STBL_TRIGGER_DIR "./lib_hrtbls"

#define DEFAULT_INIT_SERVICE_DIR "init.d"

#define HRINIT_PROPERTY_SOCK "./var/property_sock"

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
struct _hrinit {
    int epoll_fd;
    int property_sock;
    int signal_fd;
};
static struct j2sobject *_service_list = NULL;
static struct _hrinit _priv = {-1};

// 获取元素 ele 在结构体中偏移量
#define _J2SOBJECT_SERVICE_DATA_OFFSET(ele) \
    offsetof(struct j2sobject_service, ele)

// 获取结构体中 ele 元素大小
#define _J2SOBJECT_SERVICE_DATA_LEN(ele) \
    sizeof(((struct j2sobject_service *)0)->ele)

#define _J2SOBJECT_SERVICE_DATA_ARRAY_LEN(ele) \
    sizeof(((struct j2sobject_service *)0)->ele) / sizeof(((struct j2sobject_service *)0)->ele[0])

typedef struct j2sobject_service {
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
} j2sobject_service_t;

static int j2sobject_service_ctor(struct j2sobject *obj);

static struct j2sobject_prototype _j2sobject_service_prototype = {
    .name = "service",
    .type = J2S_OBJECT,
    .size = sizeof(struct j2sobject_service),
    .ctor = j2sobject_service_ctor,
    .dtor = NULL};

static struct j2sobject_fields_prototype _j2sobject_service_fields_prototype[] = {
    {.name = "name", .type = J2S_STRING, .offset = _J2SOBJECT_SERVICE_DATA_OFFSET(name), .offset_len = _J2SOBJECT_SERVICE_DATA_ARRAY_LEN(name)},
    {.name = "class", .type = J2S_STRING, .offset = _J2SOBJECT_SERVICE_DATA_OFFSET(class), .offset_len = _J2SOBJECT_SERVICE_DATA_ARRAY_LEN(class)},
    {.name = "disabled", .type = J2S_INT, .offset = _J2SOBJECT_SERVICE_DATA_OFFSET(disabled), .offset_len = 0},
    {.name = "oneshot", .type = J2S_INT, .offset = _J2SOBJECT_SERVICE_DATA_OFFSET(oneshot), .offset_len = 0},
    {.name = "argv", .type = J2S_ARRAY | J2S_STRING, .offset = _J2SOBJECT_SERVICE_DATA_OFFSET(argv), .offset_len = _J2SOBJECT_SERVICE_DATA_ARRAY_LEN(argv) /*string buffer will dynamic allocated when needed*/},
    // flag not stored
    {0}};

static int j2sobject_service_ctor(struct j2sobject *obj) {
    if (!obj)
        return -1;
    obj->name = "service";
    obj->field_protos = _j2sobject_service_fields_prototype;
    return 0;
}

static int _load_service(const char *directory) {
    DIR *dir = NULL;
    struct dirent *entry = NULL;
    struct stat st;

    if (!directory) return -1;

    if (stat(directory, &st) || !S_ISDIR(st.st_mode)) {
        HR_LOGE("it's not a good directory ...\n");
        return -1;
    }

    dir = opendir(directory);
    if (!dir) {
        HR_LOGE("can not open directory ...\n");
        return -1;
    }

    while ((entry = readdir(dir))) {
        char path[512] = {0};
        size_t len = 0;

        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;

        len = strlen(entry->d_name);

        snprintf(path, sizeof(path), "%s/%s", directory, entry->d_name);

        struct j2sobject_service *svc = (struct j2sobject_service *)j2sobject_create(&_j2sobject_service_prototype);

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
        _service_list->prev->next = J2SOBJECT(svc);
        J2SOBJECT(svc)->prev = _service_list->prev;
        J2SOBJECT(svc)->next = _service_list;
        _service_list->prev = J2SOBJECT(svc);
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

static int _property_sock() {
    int ret = -1;
    int type = SOCK_STREAM | SOCK_CLOEXEC | SOCK_NONBLOCK;
    struct sockaddr_un addr;

    int fd = socket(AF_UNIX, type, 0);
    if (fd < 0) return -1;

    fcntl(fd, F_SETFD, FD_CLOEXEC);
    fcntl(fd, F_SETFL, O_NONBLOCK);

    memset((void *)&addr, 0, sizeof(addr));

    addr.sun_family = AF_UNIX;
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", HRINIT_PROPERTY_SOCK);

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
        printf("read data error ...size.\n");
        return NULL;
    }

    printf("len:%d\n", len);
    ptr = calloc(1, len);
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

static struct j2sobject_service *_service_find_by_name(const char *name) {
    struct j2sobject *n = NULL;
    struct j2sobject_service *svc = NULL;

    if (!name) return NULL;

    for (n = _service_list->next; n != _service_list; n = n->next) {
        svc = (struct j2sobject_service *)n;
        if (0 == strcmp(svc->name, name)) {
            return svc;
        }
    }

    return NULL;
}
static struct j2sobject_service *_service_find_by_pid(pid_t pid) {
    struct j2sobject *n = NULL;
    struct j2sobject_service *svc = NULL;

    for (n = _service_list->next; n != _service_list; n = n->next) {
        svc = (struct j2sobject_service *)n;
        if (pid == svc->pid) {
            return svc;
        }
    }

    return NULL;
}

static void _service_start(struct j2sobject_service *svc) {
    pid_t pid = -1;

    if (!svc) return;

    printf("try to start :%s\n", svc->name);

    if (svc->flags & HRSVC_RUNNING) {
        return;
    }

    svc->time_started = 0;

    pid = fork();

    printf("try to start :%s, pid:%d\n", svc->name, pid);
    // child pid
    if (pid == 0) {
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

static void _service_stop(struct j2sobject_service *svc) {
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
static int _message_available(int fd) {
    // int val = 0;
    int cmd = 0;
    int len = 0;
    int reply = IPC_MESSAGE_CMD_ACK;
    char *name = NULL;

    if (fd < 0) {
        printf("_data_available: invalid fd:%d\n", fd);
        return -1;
    }

    int ret = _recv_fully(fd, (void *)&cmd, sizeof(cmd));
    if (ret != 0) {
        printf("read data error ...cmd .\n");
        return -1;
    }

    printf("cmd:0x%X\n", cmd);
    if ((cmd & 0xFFFF0000) != IPC_MESSAGE_CMD_HEAD) {
        printf("invalid header ignore ...\n");
        return -1;
    }

    switch (cmd) {
        case HRINIT_MESSAGE_SERVICE_START: {
            struct j2sobject_service *svc = NULL;
            name = _recv_string(fd);
            if (!name) return -1;
            printf("name:%s\n", (char *)name);
            svc = _service_find_by_name(name);
            free(name);
            name = NULL;
            _service_start(svc);
            break;
        }

        case HRINIT_MESSAGE_SERVICE_STOP: {
            struct j2sobject_service *svc = NULL;
            name = _recv_string(fd);
            if (!name) return -1;
            printf("name:%s\n", (char *)name);

            svc = _service_find_by_name(name);
            free(name);
            name = NULL;

            _service_stop(svc);
            break;
        }
        case HRINIT_MESSAGE_SERVICE_STATUS: {
            struct j2sobject_service *svc = NULL;
            name = _recv_string(fd);
            if (!name) return -1;
            printf("name:%s\n", (char *)name);

            svc = _service_find_by_name(name);
            free(name);
            name = NULL;

            printf("service %s(pid:%d) status:0x%x\n", svc->name, svc->pid, svc->flags & HRSVC_RUNNING);
            break;
        }
        default:
            break;
    }

    if (name)
        free(name);
    // send ack
    TEMP_FAILURE_RETRY(send(fd, &reply, sizeof(reply), 0));

    return 0;
}

static int signal_fd = -1;
static int signal_recv_fd = -1;

static void sigchld_handler(int s) {
    printf("child signal:%d\n", s);
    write(signal_fd, &s, 1);
}

static int wait_for_one_process(int block) {
    pid_t pid;
    int status;
    struct socketinfo *si;
    time_t now;
    struct listnode *node;
    struct command *cmd;
    printf("%s(%d): come in .........\n", __FUNCTION__, __LINE__);

    while ((pid = waitpid(-1, &status, block ? 0 : WNOHANG)) == -1 && errno == EINTR)
        ;
    if (pid <= 0) return -1;
    printf("waitpid returned pid %d, status = %08x\n", pid, status);

    struct j2sobject_service *svc = _service_find_by_pid(pid);
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
        _service_start(svc);
    }
    return 0;
}
void handle_signal(void) {
    char tmp[32];

    /* we got a SIGCHLD - reap and restart as needed */
    read(signal_recv_fd, tmp, sizeof(tmp));
    while (!wait_for_one_process(0))
        ;
}
static int signal_init(void) {
    int s[2];

    struct sigaction act;
    memset(&act, 0, sizeof(act));
    act.sa_handler = sigchld_handler;
    act.sa_flags = SA_NOCLDSTOP;
    sigaction(SIGCHLD, &act, 0);

    /* create a signalling mechanism for the sigchld handler */
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, s) == 0) {
        signal_fd = s[0];
        signal_recv_fd = s[1];
        fcntl(s[0], F_SETFD, FD_CLOEXEC);
        fcntl(s[0], F_SETFL, O_NONBLOCK);
        fcntl(s[1], F_SETFD, FD_CLOEXEC);
        fcntl(s[1], F_SETFL, O_NONBLOCK);
    }

    handle_signal();

    return signal_recv_fd;
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

    printf("epoll:%d\n", _priv.epoll_fd);
    _priv.property_sock = _property_sock();
    if (_priv.property_sock < 0) {
        close(_priv.epoll_fd);
        return -1;
    }
    _priv.signal_fd = signal_init();
    printf("sock:%d\n", _priv.property_sock);
    _service_list = j2sobject_create_array(&_j2sobject_service_prototype);
    assert(_service_list);

    _load_service(DEFAULT_INIT_SERVICE_DIR);

    memset((void *)&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.fd = _priv.property_sock;
    epoll_ctl(_priv.epoll_fd, EPOLL_CTL_ADD, _priv.property_sock, &ev);

    memset((void *)&ev, 0, sizeof(ev));
    ev.events = EPOLLIN;
    ev.data.fd = _priv.signal_fd;
    epoll_ctl(_priv.epoll_fd, EPOLL_CTL_ADD, _priv.signal_fd, &ev);

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
            printf("%d vs %d is ready!\n", e->data.fd, _priv.property_sock);
            // new connection ?
            if (_priv.property_sock == e->data.fd) {
                int fd = accept4(e->data.fd, NULL, NULL, SOCK_CLOEXEC);
                if (fd < 0) {
                    continue;
                }

                printf("fd:%d\n", fd);
                memset((void *)&ev, 0, sizeof(ev));
                ev.events = EPOLLIN;
                ev.data.fd = fd;
                if (epoll_ctl(_priv.epoll_fd, EPOLL_CTL_ADD, fd, &ev) == -1) {
                    close(fd);
                    continue;
                }
            } else if (_priv.signal_fd == e->data.fd) {
                printf("child event, clean child process \n");

                handle_signal();

            } else {  // service stream

                // we should read stream
                printf("please read all stream ...%d\n", e->data.fd);
                int ret = _message_available(e->data.fd);
                if (ret < 0) {
                    // we should remove fd from epoll
                    epoll_ctl(_priv.epoll_fd, EPOLL_CTL_DEL, ev.data.fd, NULL);
                    close(ev.data.fd);
                }
                sleep(2);
                // exit(1);
            }
        }
    }
    // all elements on the link will be freed
    j2sobject_free(_service_list);
    _service_list = NULL;
    close(_priv.epoll_fd);
    _priv.epoll_fd = -1;
    return 0;
}
