
#include "hr_buffer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define STRING_EXTRA_SIZE (64)

int hrbuffer_alloc(struct hrbuffer *buf, size_t size) {
    if (!buf)
        return -1;

    // assert(!buf->data);

    // assert(buf->size > 0);
    memset((void *)buf, 0, sizeof(*buf));
    buf->offset = 0;
    buf->size = size;
    buf->data = (char *)calloc(1, buf->size);
    if (!buf->data) {
        printf("%s(%d): can not allocate memory ...\n", __FUNCTION__, __LINE__);
        return -1;
    }
    buf->preallocated = 0;

    return 0;
}

int hrbuffer_prealloc(struct hrbuffer *buf, void *data, size_t size) {
    if (!buf)
        return -1;

    // assert(!buf->data);

    // assert(buf->size > 0);
    memset((void *)buf, 0, sizeof(*buf));
    buf->offset = 0;
    buf->size = size;
    buf->data = data;
    buf->preallocated = 1;

    return 0;
}

int hrbuffer_realloc(struct hrbuffer *buf, size_t size) {
    void *data = NULL;
    if (!buf /*|| !buf->data*/)
        return -1;

    if (buf->preallocated) {
        // not support!
        return -1;
    }

    data = (char *)realloc(buf->data, size);
    if (!data) {
        printf("%s(%d): can not reallocate memory ...\n", __FUNCTION__, __LINE__);
        return -1;
    }
    buf->data = data;
    buf->size = size;
    memset((void *)((const char *)buf->data + buf->offset), 0, buf->size - buf->offset);
    return 0;
}
int hrbuffer_free(struct hrbuffer *buf) {
    if (!buf)
        return -1;
    buf->offset = 0;
    if (buf->preallocated != 0) {
        return 0;
    }
    memset((void *)buf->data, 0, buf->size);

    free(buf->data);

    memset((void *)buf, 0, sizeof(*buf));
    return 0;
}
int hrbuffer_reset(struct hrbuffer *buf) {
    if (!buf || !buf->data)
        return -1;
    buf->offset = 0;
    memset((void *)buf->data, 0, buf->size);

    return 0;
}

int hrbuffer_append(struct hrbuffer *buf, void *data, size_t size) {
    if (!buf || !buf->data || !data || size == 0)
        return -1;
    if (size > buf->size - buf->offset) {
        int ret = hrbuffer_realloc(buf, buf->size + size);
        if (ret != 0) {
            return -1;  // memory not enough
        }
    }
    memcpy((void *)(buf->data + buf->offset), data, size);
    buf->offset += size;
    return 0;
}

int hrbuffer_append_string(struct hrbuffer *buf, const char *str) {
    int length = 0;
    if (!buf || !str) return -1;

    length = strlen(str); // '\0'
    if (length > buf->size - buf->offset) {
        int ret = hrbuffer_realloc(buf, buf->size + length + STRING_EXTRA_SIZE + 1);
        if (ret != 0) {
            return -1;  // memory not enough
        }
    }
    memcpy((void *)(buf->data + buf->offset), (void*)str, length);
    buf->offset += length;
   
    return 0;
}

