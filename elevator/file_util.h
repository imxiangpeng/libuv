
#include <stdio.h>

ssize_t futil_read(const char *path, char **buf);
ssize_t futil_write_fd(int fd, char *data, size_t count);
ssize_t futil_write(const char* path, void *data, size_t count);