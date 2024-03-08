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

#ifndef HRSVC_UNIX_SOCK
#define HRSVC_UNIX_SOCK "./var/svcd.sock"
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

#define HRINIT_MESSAGE_BASE 0x12340000
#define HRINIT_MESSAGE_SERVICE 0x12340000
#define HRINIT_MESSAGE_SERVICE_START (HRINIT_MESSAGE_SERVICE | 0x01)
#define HRINIT_MESSAGE_SERVICE_STOP (HRINIT_MESSAGE_SERVICE | 0x02)
#define HRINIT_MESSAGE_SERVICE_STATUS (HRINIT_MESSAGE_SERVICE | 0x03)

static int _hrsvc_socket = -1;

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

static int _hrsvc_init_ensured() {
    int ret = -1;
    int type = SOCK_STREAM | SOCK_CLOEXEC;
    struct sockaddr_un addr;
    int fd = -1;

    if (_hrsvc_socket != -1) {
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
    snprintf(addr.sun_path, sizeof(addr.sun_path), "%s", HRSVC_UNIX_SOCK);

    // maybe failed because ahsapd may not startup
    ret = TEMP_FAILURE_RETRY(
        connect(fd, (const struct sockaddr *)&addr, sizeof(addr)));
    if (ret != 0) {
        close(fd);
        // printf("can not connect ....remote ...\n");
        return -1;
    }

    _hrsvc_socket = fd;

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
    if (!val) return -1;

    len = strlen(val);
    ret = TEMP_FAILURE_RETRY(send(_hrsvc_socket, (const void *)&len, sizeof(len), 0));
    if (ret < 0) {
        return ret;
    }
    ret = TEMP_FAILURE_RETRY(send(_hrsvc_socket, (const void *)val, len, 0));
    if (ret < 0) {
        return ret;
    }

    return 0;
}

static int _hrsvc_operator(int op, const char *name) {
    int ret = -1;
    int length = 0;
    int size = -1;
    char *response = NULL;
    if (_hrsvc_socket != -1) {
        if (1 != _sock_is_connected(_hrsvc_socket)) {  // broken ?
            close(_hrsvc_socket);
            _hrsvc_socket = -1;
        }
    }

    if (_hrsvc_init_ensured() != 0) {
        return -1;
    }

    ret = TEMP_FAILURE_RETRY(send(_hrsvc_socket, (const void *)&op, sizeof(op), 0));
    if (ret < 0) {
        goto error;
    }

    // send name
    _send_string(name);

    ret = _recv_fully(_hrsvc_socket, &size, sizeof(size));
    if (ret != 0) {
        goto error;
    }

    if (size == 0) {
        return 0;
    }

    response = calloc(1, size + 1); // '\0'
    if (!response) return -1;

    ret = _recv_fully(_hrsvc_socket, response, size);

    printf("%s\n", response);

    free(response);
    return 0;
error:
    close(_hrsvc_socket);
    _hrsvc_socket = -1;
    return -1;
}
int main(int argc, char **argv) {
    _hrsvc_operator(HRINIT_MESSAGE_SERVICE_START, "hrupdate");
    printf("press any key, quering hrupdate status\n");
    getchar();
    _hrsvc_operator(HRINIT_MESSAGE_SERVICE_STATUS, "hrupdate");
    printf("press any key, stop hrupdate\n");
    getchar();
    _hrsvc_operator(HRINIT_MESSAGE_SERVICE_STOP, "hrupdate");
    getchar();
    _hrsvc_operator(HRINIT_MESSAGE_SERVICE_STATUS, "hrupdate");
    close(_hrsvc_socket);
    return 0;
}
