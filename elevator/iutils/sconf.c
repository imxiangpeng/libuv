// mxp, 20250520, smart/simple conf utils

#define _GNU_SOURCE

#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

#include <ctype.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "sconf.h"

#include "file_util.h"

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

    ssize_t len = 0;
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

    for (int i = 0; i < max; i++) {
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
        return -1;
    }

    switch (proto[idx].type) {
        case PROTO_VALUE_INT64: {
            int64_t val = strtoll(p, &endptr, 10);
            if (p == endptr || val > INT64_MAX) {
                return -1;
            }
            proto[idx].value.int64 = val;
            return 0;
        }
        case PROTO_VALUE_NUMBER: {
            double val = strtod(p, &endptr);
            if (p == endptr || errno == ERANGE) {
                return -1;
            }
            proto[idx].value.number = val;
            return 0;
        }

        case PROTO_VALUE_STRING:
            if (proto[idx].value.string != NULL) {
                free(proto[idx].value.string);
                proto[idx].value.string = NULL;
            }

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
static int match_filed(char* line, struct sconf_proto* proto, size_t size) {
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

#if 0
    p = strchr(line, '=');
    if (!p || line == p) {
        return 0;
    }
    
    
    while (p > line && *(p-1) && isspace((unsigned char)*(p-1))) {
        --p;
    }
#endif
    // find space or =
    while (*p && *p != '=' && !isspace(*p))
        p++;

    for (size_t i = 0; i < size; i++) {
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
            if (0 == match_filed(line, proto, size)) {
                // directly write origin data
                futil_write_fd(fd, line, strlen(line));
                futil_write_fd(fd, "\n", 1);  // append line eof
            }
        }

        free(buf);
    }

    for (int i = 0; i < size; i++) {
        switch (proto[i].type) {
            case PROTO_VALUE_INT64: {
                int len = snprintf(buffer, sizeof(buffer), "%s=%ld\n", proto[i].name, proto[i].value.int64);
                futil_write_fd(fd, buffer, len);
                break;
            }
            case PROTO_VALUE_NUMBER: {
                int len = snprintf(buffer, sizeof(buffer), "%s=%f\n", proto[i].name, proto[i].value.number);
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
