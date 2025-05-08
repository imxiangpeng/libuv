#ifndef CONF_H
#define CONF_H

#include <sys/types.h>
int conf_load_int64(const char* path, const char** fields, int64_t* result, size_t max);

int conf_save_int64(const char* path, const char** fields, int64_t* result, size_t max);
#endif