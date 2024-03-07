#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

// please make sure all macro same with inspur's code
// mxp, 20231030, notify event to ahsapd channel
#define HRINIT_PROPERTY_SOCK "./var/property_sock"

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

static int _ipc_channel_fd = -1;

static int _poll_in(int fd, int timeout) {
    int nr = 0;
    struct pollfd ufds[1];
    ufds[0].fd = fd;
    ufds[0].events = POLLIN;
    ufds[0].revents = 0;

    nr = poll(ufds, 1, timeout);

    return nr;
}

// not readable direct return
// readable but read fail socket is bad
// 1: connected
// other: non connected
static int _sock_is_connected(int fd) {
    if (fd < 0) {
        return 0;
    }
    if (1 == _poll_in(fd, 0)) {
        char c = 0;
        if (recv(fd, &c, 1, MSG_PEEK) <= 0) {
            return 0;
        }
    }

    return 1;
}

static int _ipc_channel_init_ensured() {
    int ret = -1;
    int type = SOCK_STREAM | SOCK_CLOEXEC;
    struct sockaddr_un addr;
    int fd = -1;

    if (_ipc_channel_fd != -1) {
        // printf("already connected .. ...\n");
        return 0;
    }

    fd = socket(AF_UNIX, type, 0);
    if (fd < 0) return -1;

    fcntl(fd, F_SETFD, FD_CLOEXEC);
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | O_NONBLOCK);

    memset((void *)&addr, 0, sizeof(addr));

    addr.sun_family = AF_UNIX;
    // or strcpy
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", HRINIT_PROPERTY_SOCK);

    // maybe failed because ahsapd may not startup
    ret = TEMP_FAILURE_RETRY(
        connect(fd, (const struct sockaddr *)&addr, sizeof(addr)));
    printf("connect:%d\n", ret);
    if (ret != 0) {
        close(fd);
        // printf("can not connect ....remote ...\n");
        return -1;
    }

    _ipc_channel_fd = fd;

    return 0;
}
static int _recv_fully(int fd, void *data_ptr, size_t size) {
    size_t bytes_left = size;
    char *data = (char *)data_ptr;

    if (fd < 0) return -1;
    while (bytes_left > 0) {
        // error or timeout
        if (_poll_in(fd, 5000) <= 0) {
            return -1;
        }

        int result = TEMP_FAILURE_RETRY(recv(fd, data, bytes_left, /*MSG_WAITALL*/ MSG_DONTWAIT));
        if (result <= 0) {
            return -1;
        }

        bytes_left -= result;
        data += result;
    }

    return bytes_left == 0 ? 0 : -1;
}
static int _send_string(const char *val) {
    int ret = -1;
    int len = 0;
    len = strlen(val);
    ret = TEMP_FAILURE_RETRY(send(_ipc_channel_fd, (const void *)&len, sizeof(len), 0));
    if (ret < 0) {
        return ret;
    }
    ret = TEMP_FAILURE_RETRY(send(_ipc_channel_fd, (const void *)val, len, 0));
    if (ret < 0) {
        return ret;
    }

    return 0;
}


// hrsvc 
static int hrinit_control(int op, const char *name) {
    int ret = -1;
    int length = 0;
    int ack = -1;
    if (_ipc_channel_fd != -1) {
        if (1 != _sock_is_connected(_ipc_channel_fd)) {  // broken ?
            close(_ipc_channel_fd);
            _ipc_channel_fd = -1;
        }
    }

    if (_ipc_channel_init_ensured() != 0) {
        return -1;
    }

    ret = TEMP_FAILURE_RETRY(send(_ipc_channel_fd, (const void *)&op, sizeof(op), 0));
    if (ret < 0) {
        goto error;
    }

    // send name
    _send_string(name);
    // send value
    // _send_string(val);

    ret = _recv_fully(_ipc_channel_fd, &ack, sizeof(ack));
    if (ret != 0) {
        goto error;
    }

    return 0;
error:
    close(_ipc_channel_fd);
    _ipc_channel_fd = -1;
    return -1;
}
int main(int argc, char **argv) {
    hrinit_control(HRINIT_MESSAGE_SERVICE_START, "hrupdate");
    printf("press any key, quering hrupdate status\n");
    getchar();
    hrinit_control(HRINIT_MESSAGE_SERVICE_STATUS, "hrupdate");
    printf("press any key, stop hrupdate\n");
    getchar();
    hrinit_control(HRINIT_MESSAGE_SERVICE_STOP, "hrupdate");
    getchar();
    close(_ipc_channel_fd);
    return 0;
}
