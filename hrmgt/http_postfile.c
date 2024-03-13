#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUF_SIZE 24 * 1024
enum {
    S_BOUNDARY = 0,
    S_BOUNDARY_META,
    S_CTX_TYPE,
    S_BOUNDARY_END,
    S_CTX_PAYLOAD,
};

// return line with \r\n
// return 0 means we need read more data
int _memcontext_without_crlf(void *data, size_t max) {
    char *ptr = data;
    char *rn = (char *)memmem(data, max, "\r\n", 2);
    if (rn) {
        return rn - (char *)data;
    }

    // not found '\r\n', but last char is '\r'
    // then consume all bytes before '\r'
    // notify we should swrap last '\r' to begin and read more data
    if (ptr[max - 1] == '\r') {
        return max - 1;
    }

    // not found any \r at end or \r\n
    return max;
}

// parse data which size is max, finding \r\n, and return all data
int _memline_with_crlf(char **ptr, size_t *size, void *data, size_t max) {
    int len = 0;
    if (!ptr || !size || !data) {
        return -1;
    }

    len = _memcontext_without_crlf(data, max);
    if (len == max) {
        // data not enough ...
    } else {
        len += 2;  // skip \r\n
    }

    if (*size < len + 1) {
        *ptr = realloc(*ptr, len + 1);
        *size = len + 1;
    }

    memcpy(*ptr, data, len);

    *(*ptr + len) = '\0';

    return len;
}

int main(int argc, char **argv) {
    char *data = calloc(1, BUF_SIZE);
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
    size_t pos = 0;
    size_t rewind = 0;
    size_t line_max = 1024;
    char *line = calloc(1, line_max);

    int b = 0;
    while (!feof(rfp) || rewind != 0) {
        size_t total_size = 0;
        printf("try read :%ld(%ld)\n", BUF_SIZE - rewind, rewind);

    readdata:

        total_size = 0;

        total_size = fread(data + rewind, 1, BUF_SIZE - rewind, rfp);
        total_size += rewind;
        // must reset rewind flag
        rewind = 0;

        // read from begin
        pos = 0;

        if (total_size == 0) {
            printf("no data anymore ...\n");
            break;
        }

        while (pos < total_size) {
            switch (state) {
                case S_BOUNDARY: {  // search boundary
                    // must begin with --, otherwise data broken
                    if (data[pos] != '-' || data[pos + 1] != '-') {
                        printf("boundary error not expected data \n");
                        goto out;  // error
                    }

                    pos += 2;  // skip --

                    // verify boundary string
                    if (memcmp((void *)data + pos, boundary, strlen(boundary)) != 0) {
                        printf("boundary error not expected data, found -- but not boundary ...\n");
                        goto out;  // error
                    }

                    // skip boundary to verify wether it's end with --
                    pos += strlen(boundary);
                    // if boundary end with --, it's end
                    if (data[pos] == '-' && data[pos + 1] == '-') {
                        printf("all boundary end ...\n");
                        // success
                        goto out;
                    }
                    // not end with --, it means that it's a new part
                    // now it must be \r\n
                    pos += 2;  // skip \r\n
                    state = S_BOUNDARY_META;
                    break;
                }
                case S_BOUNDARY_META: {  // metadata
                    int ret = 0;
                    memset((void *)line, 0, line_max);
                    // read all lines until empty \r\n line
                    while ((ret = _memline_with_crlf(&line, &line_max, data + pos, total_size - pos)) != 2) {
                        // printf("read line:total_size - pos: %ld vs %d -> %s\n", total_size - pos, ret, line);
                        printf("line ->:%s\n", line);
                        if (ret == total_size - pos) {
                            printf("data not enough, read more ...\n");
                            // not found line end, data is not enough!
                            memcpy(data, data + pos, total_size - pos);
                            rewind = total_size - pos;
                            total_size = 0;
                            goto readdata;
                        }
                        pos += ret;
                        memset((void *)line, 0, line_max);
                    }

                    // meet empty \r\n
                    pos += 2;  // skip empty \r\n

                    state = S_CTX_PAYLOAD;

                    break;
                }

                case S_CTX_PAYLOAD: {
                    // return data length before \r\n, not contains \r\n
                    ssize_t available = _memcontext_without_crlf((void *)(data + pos), total_size - pos);
                    if (available > 0) {
                        // we should directly write those data
                        ssize_t n = write(fd, (void *)(data + pos), available);
                        pos += n;

                        // not consume all data, we should rewind left data
                        if (total_size - pos > 0) {
                            // and rewind left data
                            memcpy(data, data + pos, total_size - pos);
                            rewind = total_size - pos;
                            total_size = 0;
                        }
                    } else {
                        // available == 0, match \r\n at begin
                        // we move \r\n to buffer begin, because it's sample to match and no need care buffer end
                        printf("found '\\r\\n', stopped ?\n");
                        char *ptr = data + pos;
                        // assert *ptr == '\r';
                        // assert *(ptr + 1) == '\n';
                        if (*(ptr + 2) == '-' && *(ptr + 3) == '-') {
                            if (memcmp(ptr + 4, boundary, strlen(boundary)) == 0) {
                                printf("real stop .or next trunck..\n");
                                pos += 2;  // skip \r\n, -- and boundary will be parsed in next loop
                                state = S_BOUNDARY;
                                break;
                            }
                        } else {
                            ssize_t s = write(fd, (void *)(data + pos), 2 + 2);
                            pos += s;
                        }
                    }

                    break;
                }
                default:
                    break;
            }
        }
    }

out:

    fclose(rfp);
    close(fd);

    free(data);
    return 0;
}
