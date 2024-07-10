#ifndef HRURL_REQUEST_H
#define HRURL_REQUEST_H

#include "hr_buffer.h"

typedef enum {
    UR_METHOD_GET = 0,
    UR_METHOD_POST,
    UR_METHOD_PUT,
} url_request_method_e;

enum {
    URLOPT_VERBOSE = 0,
    URLOPT_TIMEOUT,          // ms
    URLOPT_CONNECT_TIMEOUT,  // ms
    URLOPT_MAX,
};

struct url_request {
    // struct hrbuffer url;
    url_request_method_e method;
    // set option
    int (*set_query)(struct url_request *, const char *, const char *);
    int (*set_form)(struct url_request *, const char *, const char *);
    int (*set_header)(struct url_request *, const char *, const char *);
    int (*set_option)(struct url_request *, unsigned int opt, long value);
    int (*allow_redirect)(struct url_request *, int);
    int (*get)(struct url_request *, const char *url, struct hrbuffer *b);
    int (*post)(struct url_request *, const char *url, struct hrbuffer *b);
    int (*put)(struct url_request *, const char *url, struct hrbuffer *b, size_t size, size_t (*read_callback)(void *ptr, size_t size, size_t nmemb, void *userdata), void *args);
    int (*request)(struct url_request *, const char *url, struct hrbuffer *b);
};

struct url_request_pool {
    struct url_request *(*acquire)(struct url_request_pool *self);
    void (*release)(struct url_request_pool *self, struct url_request *request);
};

struct url_request *url_request_new(void);
void url_request_free(struct url_request *req);

struct url_request_pool *url_request_pool_create(int max);
void url_request_pool_destroy(struct url_request_pool *pool);

#endif
