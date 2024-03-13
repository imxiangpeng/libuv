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
    len += 2; // skip \r\n
    printf("len:%d\n", len);
    if (*size < len + 1) {
        *ptr = realloc(*ptr, len + 1);
        *size = len + 1;
    }

    memcpy(*ptr, data, len);
    // *(*ptr-1) = '\0';

    *(*ptr + len) = '\0';

    return len;
}

int main(int argc, char **argv) {
#define BUF_SIZE 24 * 1024
    char *buf = calloc(1, BUF_SIZE);
    int idx = 0;

    const char *boundary = "----WebKitFormBoundaryTmNn7iRszae1RJ8N";

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

#if 0
    size_t s = 512;
    char* line = (char*)calloc(1, s);
    printf("line:%p\n", line);
    int i = 0;
    while (!feof(rfp)) {
        int ret = getline(&line, &s, rfp);
        i++;
        if (i > 4) break;
        printf("line -> (%ld):%s\n", strlen(line), line);
        printf("line:");
        for (int i = 0; i < strlen(line); i++) {
            printf("0x%X ", line[i]);
        }
        printf("\n");
    }
    printf("line:%p, size:%ld\n", line, s);
    free(line);
#endif

    int state = S_INIT;
    size_t l = 0;
    size_t reserve = 0;
    size_t line_max = 1024;
    char *line = calloc(1, line_max);

    int b = 0;
    while (!feof(rfp) || reserve != 0) {
        printf("try read :%ld(%ld)\n", BUF_SIZE - reserve, reserve);
        size_t ss = fread(buf + reserve, 1, BUF_SIZE - reserve, rfp);
        printf("read :%ld\n", ss);
        ss += reserve;
        reserve = 0;
        printf("read2 :%ld\n", ss);
        // rewind
        l = 0;

        while (l < ss) {
            printf("state:%d, l:%ld\n", state, l);
            switch (state) {
                case S_INIT: {
                    if (buf[l] != '-' || buf[l + 1] != '-') {
                        l++;
                        continue;
                    }
                    l += 2;  // skip --
                    state = S_BOUNDARY;
                    break;
                }
                case S_BOUNDARY: {  // search boundary
                    // verify new data
                    printf("buf +l:%s vs %s\n", buf + l, boundary);
                    if (0 == strstr(buf + l, boundary)) {
                        printf("no more part!\n");
                        return -1;
                    }
                    l += strlen(boundary);
                    l += 2;  // skip \r\n
                    state = S_CTX_DISP;
                    break;
                }
                case S_CTX_DISP: {
                    memset((void *)line, 0, line_max);
                    int ret = _memline(&line, &line_max, buf + l, ss - l);
                    printf("read line:%d -> %s\n", ret, line);

                    char *p = strstr(line, "Content-Disposition:");
                    if (!p) {
                        printf("%s(%d): ...invalid content disposition.....\n", __FUNCTION__,
                               __LINE__);
                        return -01;
                    }
                    char *form = p + strlen("Content-Disposition:") + 1;
                    printf("form:%s\n", form);
                    l += ret;
                    state = S_CTX_TYPE;
                    break;
                }
                case S_CTX_TYPE: {
                    memset((void *)line, 0, line_max);
                    int ret = _memline(&line, &line_max, buf + l, ss - l);
                    printf("read line:%d -> %s\n", ret, line);
                    l += ret;
                    if (ret == 2) {
                        state = S_CTX_PAYLOAD;
                    } else {
                        state = S_BOUNDARY_END;
                    }
                    break;
                }
                case S_BOUNDARY_END: {

                    memset((void *)line, 0, line_max);
                    int ret = _memline(&line, &line_max, buf + l, ss - l);
                    printf("bound end line:%d -> %s\n", ret, line);
                    if (buf[l] != '\r' && buf[l + 1] != '\n')
                        return -1;
                    l += 2;
                    state = S_CTX_PAYLOAD;
                    break;
                }

                case S_CTX_PAYLOAD: {
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
                        // *ptr == '\r';
                        // *(ptr + 1) == '\n';
                        if (*(ptr + 2) == '-' && *(ptr + 3) == '-') {
                            if (memcmp(ptr + 4, boundary, strlen(boundary)) == 0) {
                                printf("real stop ...\n");
                                return 0;
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
