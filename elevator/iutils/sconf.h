#ifndef SCONF_H
#define SCONF_H

#include <stdint.h>
#include <sys/types.h>

// smart/simple conf utils

struct sconf_proto {
    const char* name;

    enum {
        PROTO_VALUE_NUMBER,
        PROTO_VALUE_DECIMAL,
        PROTO_VALUE_STRING
    } type;
    union {
        int64_t number;
        double decimal;
        // dynamic allocated memory, you should free it when not used
        char* string;
    } value;
};

typedef void (*sconf_observer)(const char* path, void* priv);

int sconf_load_int64(const char* path, const char** fields, int64_t* result, size_t max);

int sconf_save_int64(const char* path, const char** fields, int64_t* result, size_t max);

int sconf_load_with_proto(const char* path, struct sconf_proto* proto, size_t size);

int sconf_save_with_proto(const char* path, struct sconf_proto* proto, size_t size);

int sconf_register_observer(const char* path, sconf_observer, void* priv);
#endif