#include <sys/stat.h>
#include <unistd.h>
#define _GNU_SOURCE
#include <ctype.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "conf.h"

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

int conf_load_int64(const char* path, const char** fields, int64_t* result, size_t max) {
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


int conf_save_int64(const char* path, const char** fields, int64_t* result, size_t max) {
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
            unlink(tmp);
            free(tmp);
            close(fd);
            return -1;
        }
    }
    close(fd);

    unlink(path);
    rename(tmp, path);
    free(tmp);
    return 0;
}