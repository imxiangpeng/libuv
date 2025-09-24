// mxp, 20250415, file utils
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "file_util.h"

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

// you must free the pointer
ssize_t futil_read(const char *path, char **buf) {
    int fd = -1;
    struct stat sb;
    char *data = NULL, *ptr = NULL;
    if (lstat(path, &sb) != 0 || sb.st_size == 0 || !buf) {
        return -1;
    }

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        return -1;
    }

    data = (char *)calloc(1, sb.st_size + 1); // reserve end \0, care!
    if (!data) {
        close(fd);
        return -1;
    }

    ptr = data;

    size_t remaining = sb.st_size;
    while (remaining > 0) {
        ssize_t n = TEMP_FAILURE_RETRY(read(fd, ptr, remaining));
        if (n <= 0) {
            free(data);
            close(fd);
            return -1;
        }
        ptr += n;
        remaining -= n;
    }
    close(fd);

    *buf = data;
    return sb.st_size;
}

ssize_t futil_write_fd(int fd, char *data, size_t count) {
    ssize_t left = count;
    char *ptr = data;

    if (fd < 0 || !data || count == 0) return -1;

    while (left > 0) {
        ssize_t n = TEMP_FAILURE_RETRY(write(fd, ptr, left));
        if (n == -1) {
            return count - left;
        }
        ptr += n;
        left -= n;
    }

    return count;
}

ssize_t futil_write(const char* path, void *data, size_t count) {
    int fd = -1;
    ssize_t result = -1;

    if (!data || !path || count <= 0) {
        return -1;
    }

    fd = open(path, O_RDWR | O_TRUNC | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd < 0) {
        return -1;
    }

    result = futil_write_fd(fd, data, count);
    fdatasync(fd);
    close(fd);

    return result;
}

// copy & modified from toolbox's rm.c
int futil_unlink(const char* name) {
    struct stat st;
    DIR* dir;
    struct dirent* de;

    if (!name) return -1;

    /* is it a file or directory? */
    if (lstat(name, &st) < 0)
        return (errno == ENOENT) ? 0 : -1;

    /* a file, so unlink it */
    if (!S_ISDIR(st.st_mode))
        return unlink(name);

    /* a directory, so open handle */
    dir = opendir(name);
    if (dir == NULL)
        return -1;

    /* recurse over components */
    while ((de = readdir(dir)) != NULL) {
        char dn[PATH_MAX] = {0};
        if (!strcmp(de->d_name, "..") || !strcmp(de->d_name, "."))
            continue;
        sprintf(dn, "%s/%s", name, de->d_name);
        futil_unlink(dn);
    }

    /* close directory handle */
    if (closedir(dir) < 0)
        return -1;

    /* delete target directory */
    return rmdir(name);
}