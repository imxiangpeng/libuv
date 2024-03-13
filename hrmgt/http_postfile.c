#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum {
    S_INIT = 0,
    S_BOUNDARY,
    S_CTX_DISP,
    S_CTX_TYPE,
    S_BOUNDARY_END,
    S_CTX_PAYLOAD,
};

// return line with \r\n
// return 0 means we need read more data
int _memcontext(void *data, size_t max) {
    char *ptr = data;
    char *rn = (char *)memmem(data, max, "\r\n", 2);
    if (rn) {
        printf("%s(%d): found '\\r\\n'\n", __FUNCTION__, __LINE__);
        return rn - (char *)data;
    }

    // not found '\r\n', but last char is '\r'
    // then consume all bytes before '\r'
    if (ptr[max - 1] == '\r') {
        printf("%s(%d): found '\\r' in last, we need more data\n", __FUNCTION__, __LINE__);
        return max - 1;
    }

    return max;
}
int _memline(char **ptr, size_t *size, void *data, size_t max) {
    int len = 0;
    if (!ptr || !size || !data) {
        return -1;
    }
#if 0
    char *p = memchr(data, '\n', max);
    if (!p)
        return 0;
    if (*(p - 1) != '\r') {
        printf("p[-1] not is '\r'\n");
    }
    len = p - (char *)data + 1;

#endif
    len = _memcontext(data, max);
    if (len == max) {
        // data not enough ...
    } else {
        len += 2;  // skip \r\n
    }
    printf("len:%d\n", len);
    if (*size < len + 1) {
        *ptr = realloc(*ptr, len + 1);
        *size = len + 1;
    }

    memcpy(*ptr, data, len);
    // strncat(*ptr, data, len);
    // *(*ptr-1) = '\0';

    *(*ptr + len) = '\0';

    return len;
}

int main(int argc, char **argv) {
#define BUF_SIZE 24 * 1024
    char *buf = calloc(1, BUF_SIZE);
    int idx = 0;

    // const char *boundary = "----WebKitFormBoundaryTmNn7iRszae1RJ8N";
    const char *boundary = "----WebKitFormBoundaryLHRXX18JscMKZPMw";

    const char *save = "real.bin";
    int fd = open(save, O_CREAT | O_TRUNC | O_RDWR, 0644);
    if (fd < 0) {
        printf("%s(%d): .......%s.\n", __FUNCTION__, __LINE__, strerror(errno));
        return -1;
    }

    FILE *rfp = fopen("web_post_file.bin", "r");
    if (!rfp) {
        printf("%s(%d): ........\n", __FUNCTION__, __LINE__);
        close(fd);
        return -1;
    }

    int state = S_BOUNDARY;
    size_t l = 0;
    size_t reserve = 0;
    size_t line_max = 1024;
    char *line = calloc(1, line_max);

    int b = 0;
    while (!feof(rfp) || reserve != 0) {
        printf("try read :%ld(%ld)\n", BUF_SIZE - reserve, reserve);

    readdata:

        size_t ss = fread(buf + reserve, 1, BUF_SIZE - reserve, rfp);
        printf("read :%ld\n", ss);
        ss += reserve;
        reserve = 0;
        printf("read2 :%ld\n", ss);
        // rewind
        l = 0;

        if (ss == 0) {
            printf("no data anymore ...\n");
            break;
        }

        while (l < ss) {
            printf("state:%d, l:%ld\n", state, l);
            switch (state) {
                case S_BOUNDARY: {  // search boundary
                    // our line is large enough if -- begin

                    if (buf[l] != '-' || buf[l + 1] != '-') {
                        printf("boundary error not expected data \n");
                        return -1;
                    }
                    l += 2;

                    if (memcmp((void *)buf + l, boundary, strlen(boundary)) != 0) {
                        printf("boundary error not expected data, found -- but not boundary ...\n");
                        return -1;
                    }
                    // verify new data
                    printf("buf +l:%s vs %s\n", buf + l, boundary);

                    l += strlen(boundary);
                    if (buf[l] == '-' && buf[l + 1] == '-') {
                        printf("all boundary end ...\n");
                        return 0;
                    }
                    l += 2;  // skip \r\n
                    state = S_CTX_DISP;
                    break;
                }
                case S_CTX_DISP: {  // metadata
                    int ret = 0;
                    memset((void *)line, 0, line_max);
                    while (l < ss && ((ret = _memline(&line, &line_max, buf + l, ss - l)) != 2)) {
                        printf("read line:ss -l: %ld vs %d -> %s\n", ss - l, ret, line);
                        if (ret == ss - l) {
                            printf("data not enough, read more ...\n");
                            // not found line end, data is not enough!
                            // memcpy(buf, buf + l, ss - l);
                            // reserve = ss - l;
                            // ss = 0;
                            // goto readdata;
                        }
                        l += ret;
                        memset((void *)line, 0, line_max);
                    }

                    printf("l:%ld, ss:%ld, ret:%d\n", l, ss, ret);
                    if (l >= ss) {
                        printf("%s(%d): maybe error data: %ld vs %ld\n", __FUNCTION__, __LINE__, l, ss);
                        // return -1;
                        goto readdata;
                    }

                    // meet empty \r\n
                    l += 2;  // skip empty \r\n

                    state = S_CTX_PAYLOAD;

                    break;
                }

                case S_CTX_PAYLOAD: {
                    // return data length before \r\n, not contains \r\n
                    ssize_t available = _memcontext((void *)(buf + l), ss - l);
                    if (available > 0) {
                        ssize_t s = write(fd, (void *)(buf + l), available);
                        printf("all: %ld, write s:%ld, l:%ld\n", ss - l, s, l);
                        l += s;
                        printf("2s:%ld, l:%ld\n", s, l);

                        memcpy(buf, buf + l, ss - l);
                        reserve = ss - l;
                        ss = 0;
                    } else {
                        printf("found '\\r\\n', stopped ?\n");
                        char *ptr = buf + l;
                        // assert *ptr == '\r';
                        // assert *(ptr + 1) == '\n';
                        if (*(ptr + 2) == '-' && *(ptr + 3) == '-') {
                            if (memcmp(ptr + 4, boundary, strlen(boundary)) == 0) {
                                printf("real stop .or next trunck..\n");
                                l += 2;  // skip \r\n
                                state = S_BOUNDARY;
                                break;
                            }
                        } else {
                            ssize_t s = write(fd, (void *)(buf + l), 2 + 2);
                            l += s;
                        }
                    }

                    break;
                }
                default:
                    break;
            }
        }

        l = 0;
    }

    fflush(stdout);
    usleep(1000);
    fclose(rfp);
    close(fd);
    return 0;
}
