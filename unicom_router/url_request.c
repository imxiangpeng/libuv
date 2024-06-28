// mxp, 20240530, simple libcurl wrapper for http request
// support adjust query & header & form data

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#include "hr_log.h"
#endif

#include <curl/curl.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

#include "hr_buffer.h"
#include "hr_list.h"
#include "url_request.h"

struct url_request_param {
    char *value;
    struct hr_list_head entry;
};
struct url_request_priv {
    // must make sure it's first element
    struct url_request request;

    CURL *curl;

    int allow_redirect;  // allow redirect
    struct hr_list_head query;

    struct hrbuffer form;
    struct curl_slist *headers;
    // char *url
    // char url[];  // input url is appended end

    struct hr_list_head pool_entry;  // can be attached to pool
};

struct url_request_pool_priv {
    struct url_request_pool pool;

    pthread_mutex_t lock;
    pthread_cond_t cond;
    struct hr_list_head head;
};

#define TCLOUD_REQUEST_PRIV(self) container_of(self, struct url_request_priv, request)
#define TCLOUD_REQUEST_POOL_PRIV(self) container_of(self, struct url_request_pool_priv, pool)

static struct url_request *url_request_pool_acquire(struct url_request_pool *self) {
    struct url_request_priv *req = NULL;
    struct url_request_pool_priv *priv = TCLOUD_REQUEST_POOL_PRIV(self);

    if (!priv) {
        return NULL;
    }

    pthread_mutex_lock(&priv->lock);

    while (hr_list_empty(&priv->head)) {
        // no available, wait available
        pthread_cond_wait(&priv->cond, &priv->lock);
    }

    req = hr_list_first_entry(&priv->head, struct url_request_priv, pool_entry);

    // take off from list
    hr_list_del(&req->pool_entry);

    pthread_mutex_unlock(&priv->lock);

    HR_LOGD("%s(%d): got req:%p\n", __FUNCTION__, __LINE__, &req->request);

    return &req->request;
}
static void url_request_pool_release(struct url_request_pool *self, struct url_request *req) {
    struct url_request_pool_priv *priv = TCLOUD_REQUEST_POOL_PRIV(self);

    if (!priv || !req) {
        return;
    }
    HR_LOGD("%s(%d): release req:%p\n", __FUNCTION__, __LINE__, req);
    pthread_mutex_lock(&priv->lock);
    hr_list_add_tail(&TCLOUD_REQUEST_PRIV(req)->pool_entry, &priv->head);
    pthread_cond_signal(&priv->cond);
    pthread_mutex_unlock(&priv->lock);
}
struct url_request_pool *url_request_pool_create(int max) {
    struct url_request_pool_priv *priv = (struct url_request_pool_priv *)calloc(1, sizeof(struct url_request_pool_priv));
    if (!priv) {
        // no memory
        return NULL;
    }

    pthread_mutex_init(&priv->lock, NULL);
    pthread_cond_init(&priv->cond, NULL);

    HR_INIT_LIST_HEAD(&priv->head);

    for (int i = 0; i < max; i++) {
        struct url_request_priv *req = (struct url_request_priv *)url_request_new();
        HR_LOGD("%s(%d): create req:%p\n", __FUNCTION__, __LINE__, req);
        hr_list_add_tail(&req->pool_entry, &priv->head);
    }

    priv->pool.acquire = url_request_pool_acquire;
    priv->pool.release = url_request_pool_release;
    return &priv->pool;
}
void url_request_pool_destroy(struct url_request_pool *self) {
    struct url_request_priv *n, *p;
    struct url_request_pool_priv *priv = TCLOUD_REQUEST_POOL_PRIV(self);

    if (!priv) {
        return;
    }

    hr_list_for_each_entry_safe(p, n, &priv->head, pool_entry) {
        hr_list_del(&p->pool_entry);
        url_request_free((struct url_request *)p);
    }

    HR_INIT_LIST_HEAD(&priv->head);

    free(priv);
}
static int _set_query(struct url_request *req, const char *name, const char *val) {
    struct url_request_param *param = NULL;
    struct url_request_priv *priv = TCLOUD_REQUEST_PRIV(req);

    if (!priv || !name) return -1;

    param = (struct url_request_param *)calloc(1, sizeof(struct url_request_param));
    HR_INIT_LIST_HEAD(&param->entry);

    asprintf(&param->value, "%s=%s", name, val ? val : "");

    hr_list_add_tail(&param->entry, &priv->query);
    return 0;
}

static int _set_form(struct url_request *req, const char *name, const char *val) {
    struct url_request_priv *priv = TCLOUD_REQUEST_PRIV(req);

    if (!priv || !name) return -1;

    if (priv->form.offset != 0) {
        hrbuffer_append_string(&priv->form, "&");
    }

    hrbuffer_append_string(&priv->form, name);
    if (val) {
        hrbuffer_append_string(&priv->form, "=");
        hrbuffer_append_string(&priv->form, val);
    }

    return 0;
}

static int _set_header(struct url_request *req, const char *name, const char *val) {
    char *str = NULL;
    struct url_request_priv *priv = TCLOUD_REQUEST_PRIV(req);

    if (!priv || !name) return -1;

    asprintf(&str, "%s: %s", name, val ? val : "");

    priv->headers = curl_slist_append(priv->headers, str);

    free(str);

    return 0;
}

static int _allow_redirect(struct url_request *req, int val) {
    struct url_request_priv *priv = TCLOUD_REQUEST_PRIV(req);

    priv->allow_redirect = !!val;
    return 0;
}
static size_t _data_receive(void *ptr, size_t size, size_t nmemb,
                            void *userdata) {
    struct hrbuffer *buf = (struct hrbuffer *)userdata;
    size_t total = size * nmemb;
    if (!ptr || !userdata)
        return total;  // drop all data

    hrbuffer_append(buf, ptr, total);
    return total;
}

static int _http_request(struct url_request *req, const char *url, struct hrbuffer *b) {
    struct url_request_priv *priv = NULL;
    CURLcode rc;
    CURL *curl = NULL;
    CURLU *_curl = NULL;
    long response_code = 0;
    char *redirect_url = NULL;
    struct url_request_param *n, *p;
    char *payload = NULL;

    // b maybe null, when you do not care data
    if (!req) return -1;

    priv = TCLOUD_REQUEST_PRIV(req);
    curl = priv->curl;
    if (!curl) {
        return -1;
    }

    if (hr_list_empty(&priv->query)) {
        curl_easy_setopt(curl, CURLOPT_URL, url);
    } else {
        _curl = curl_url();
        curl_url_set(_curl, CURLUPART_URL, url, 0);
        // release memory directly ...
        // because request maybe reused!
        hr_list_for_each_entry_safe(p, n, &priv->query, entry) {
            curl_url_set(_curl, CURLUPART_QUERY, p->value, CURLU_APPENDQUERY);
            if (p->value) free(p->value);
            hr_list_del(&p->entry);
            free(p);
        }
        curl_easy_setopt(curl, CURLOPT_CURLU, _curl);
    }

    if (priv->headers)
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, priv->headers);

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, _data_receive);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, b);
    if (!b) {
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1);
    }

    if (UR_METHOD_GET == req->method) {
        curl_easy_setopt(curl, CURLOPT_POST, 0L);
    }
    if (UR_METHOD_POST == req->method) {
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, priv->form.data);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, priv->form.offset);
    }

    // follow redirect
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);

    rc = curl_easy_perform(curl);

    if (payload) {
        free(payload);
    }

    if (_curl) {
        curl_url_cleanup(_curl);
    }

    // clean memory, because we may use this socket to other request

    if (priv->headers) {
        curl_slist_free_all(priv->headers);
        priv->headers = NULL;
    }

    hrbuffer_reset(&priv->form);

    if (rc != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(rc));
        fprintf(stderr, "curl_easy_perform() url: %s\n", url);
        return -1;
    }
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    curl_easy_getinfo(curl, CURLINFO_REDIRECT_URL, &redirect_url);
    curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &redirect_url);
    if (redirect_url != NULL) {
    }

    return 0;
}

static int _http_do_get(struct url_request *req, const char *url, struct hrbuffer *b) {
    if (!req || !req->request | !url) {
        return -1;
    }

    req->method = UR_METHOD_GET;
    return req->request(req, url, b);
}
static int _http_do_post(struct url_request *req, const char *url, struct hrbuffer *b) {
    if (!req || !req->request | !url) {
        return -1;
    }

    req->method = UR_METHOD_POST;
    return req->request(req, url, b);
}

static int _http_do_put(struct url_request *req, const char *url, struct hrbuffer *b, size_t size, size_t (*read_callback)(void *ptr, size_t size, size_t nmemb, void *userdata), void *args) {
    struct url_request_priv *priv = NULL;
    CURLcode rc;
    CURL *curl = NULL;
    CURLU *_curl = NULL;
    long response_code = 0;
    // char *redirect_url = NULL;
    struct url_request_param *n, *p;
    char *payload = NULL;

    // b maybe null, when you do not care data
    if (!req) return -1;

    priv = TCLOUD_REQUEST_PRIV(req);

    curl = priv->curl;
    if (!curl) {
        return -1;
    }

    if (hr_list_empty(&priv->query)) {
        curl_easy_setopt(curl, CURLOPT_URL, url);
    } else {
        _curl = curl_url();
        curl_url_set(_curl, CURLUPART_URL, url, 0);
        // release memory directly ...
        // because request maybe reused!
        hr_list_for_each_entry_safe(p, n, &priv->query, entry) {
            curl_url_set(_curl, CURLUPART_QUERY, p->value, CURLU_APPENDQUERY);
            if (p->value) free(p->value);
            hr_list_del(&p->entry);
            free(p);
        }
        curl_easy_setopt(curl, CURLOPT_CURLU, _curl);
    }

    if (priv->headers)
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, priv->headers);

    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, _data_receive);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, b);
    if (!b) {
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1);
    }

    curl_easy_setopt(curl, CURLOPT_PUT, 1L);
    curl_easy_setopt(curl, CURLOPT_READFUNCTION, read_callback);
    curl_easy_setopt(curl, CURLOPT_READDATA, (void *)args);
    curl_easy_setopt(curl, CURLOPT_INFILESIZE, size);

    /* enable all supported built-in compressions */
    // curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");

    // follow redirect
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);

    rc = curl_easy_perform(curl);

    if (payload) {
        free(payload);
    }

    if (_curl) {
        curl_url_cleanup(_curl);
    }

    // clean memory, because we may use this socket to other request
    if (priv->headers) {
        curl_slist_free_all(priv->headers);
        priv->headers = NULL;
    }

    hrbuffer_reset(&priv->form);

    if (rc != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(rc));
        fprintf(stderr, "curl_easy_perform() url: %s\n", url);
        return -1;
    }

    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    // curl_easy_getinfo(curl, CURLINFO_REDIRECT_URL, &redirect_url);
    // curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &redirect_url);

    return 0;
}
struct url_request *url_request_new(void) {
    struct url_request_priv *priv = (struct url_request_priv *)calloc(1, sizeof(struct url_request_priv));

    priv->curl = curl_easy_init();
    if (!priv->curl) {
        return NULL;
    }

    // complete connection within 10000 milliseconds
    curl_easy_setopt(priv->curl, CURLOPT_CONNECTTIMEOUT_MS, 30000L);
    curl_easy_setopt(priv->curl, CURLOPT_SSL_VERIFYPEER, 0);
    curl_easy_setopt(priv->curl, CURLOPT_SSL_VERIFYHOST, 0);

    priv->request.method = UR_METHOD_GET;

    priv->request.set_query = _set_query;
    priv->request.set_form = _set_form;
    priv->request.set_header = _set_header;
    priv->request.allow_redirect = _allow_redirect;
    priv->request.get = _http_do_get;
    priv->request.post = _http_do_post;
    priv->request.put = _http_do_put;
    priv->request.request = _http_request;

    priv->headers = NULL;
    HR_INIT_LIST_HEAD(&priv->query);
    hrbuffer_alloc(&priv->form, 64);

    // init for pool
    HR_INIT_LIST_HEAD(&priv->pool_entry);
    return (struct url_request *)priv;
}

void url_request_free(struct url_request *req) {
    struct url_request_param *n, *p;
    struct url_request_priv *priv = TCLOUD_REQUEST_PRIV(req);
    if (!req) return;

    // you should not release, when it's attached on one pool
    if (priv->curl) {
        curl_easy_cleanup(priv->curl);
        priv->curl = NULL;
    }

    if (priv->headers) {
        curl_slist_free_all(priv->headers);
        priv->headers = NULL;
    }

    hr_list_for_each_entry_safe(p, n, &priv->query, entry) {
        if (p->value) free(p->value);
        hr_list_del(&p->entry);
        free(p);
    }

    hrbuffer_free(&priv->form);

    HR_INIT_LIST_HEAD(&priv->pool_entry);
    free(priv);
}
