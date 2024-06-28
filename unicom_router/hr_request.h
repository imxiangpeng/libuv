#ifndef URL_REQUEST_H
#define URL_REQUEST_H

#include "hr_buffer.h"

typedef enum {
    HR_METHOD_GET = 0,
    HR_METHOD_POST,
    HR_METHOD_PUT,
} urlrequest_method_e;

struct urlrequest {
    // struct hrbuffer url;
    urlrequest_method_e method;
    // set option
    int (*set_query)(struct urlrequest *, const char *, const char *);
    int (*set_form)(struct urlrequest *, const char *, const char *);
    int (*set_header)(struct urlrequest *, const char *, const char *);
    int (*allow_redirect)(struct urlrequest *, int);
    int (*get)(struct urlrequest *, const char *url, struct hrbuffer *b);
    int (*post)(struct urlrequest *, const char *url, struct hrbuffer *b);
    int (*put)(struct urlrequest *, const char *url, struct hrbuffer *b, size_t size, size_t (*read_callback)(void *ptr, size_t size, size_t nmemb, void *userdata), void *args);
    int (*request)(struct urlrequest *, const char *url, struct hrbuffer *b);
};

struct urlrequest_pool {
    struct urlrequest *(*acquire)(struct urlrequest_pool *self);
    void (*release)(struct urlrequest_pool *self, struct urlrequest *request);
};

struct urlrequest *urlrequest_new(void);
void urlrequest_free(struct urlrequest *req);

struct urlrequest_pool *urlrequest_pool_create(int max);
void urlrequest_pool_destroy(struct urlrequest_pool *pool);

#endif
