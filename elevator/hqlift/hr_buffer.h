
#ifndef HR_BUFFER_H
#define HR_BUFFER_H
#include <stddef.h>
struct hrbuffer {
    char *data;
    size_t size;
    size_t offset;
    
    int preallocated;
};


int hrbuffer_alloc(struct hrbuffer *buf, size_t size);
int hrbuffer_prealloc(struct hrbuffer *buf, void* data, size_t size);
int hrbuffer_realloc(struct hrbuffer *buf, size_t size);
int hrbuffer_free(struct hrbuffer *buf);
int hrbuffer_reset(struct hrbuffer *buf);
int hrbuffer_append(struct hrbuffer *buf, void *data, size_t size);
int hrbuffer_append_string(struct hrbuffer *buf, const char *str);

#endif
