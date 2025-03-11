// mxp, 20250310 sample skeleton mqtt

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
// #include <json-c/json.h>
// #include <json-c/json_object.h>
#include <cjson/cJSON.h>
#include <limits.h>
#include <math.h>
#include <mosquitto.h>
#include <mqtt_protocol.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <unistd.h>
#include <uv.h>

#include "dm_topic.h"
#include "hr_list.h"
#include "hr_log.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

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

#define DM_DATA_FORMAT_UNION 1

#define MNG_URL "https://123.6.50.69:8803"
// #define MNG_URL "https://rtm.ossapp.chinaunicom.cn:8803"

#define DM_DEFAULT_CONFIG_DIR "."
#define DM_DEFAULT_CONFIG_NAME "dm.json"

#define BROKER_DEFAULT_PORT 1883
#define DEFAULT_POLL_EVENTS (UV_READABLE | UV_DISCONNECT) /*| UV_WRITABLE*/
struct dm_platform {
    char host[256];
    int port;
    int alive_time;  // 60s
    char username[256];
    char password[256];
    char client_id[128];
    int qos;

    struct mosquitto *mosq;
    int sock;  // mosquitto socket
    int mosq_have_connected;
    uv_loop_t *loop;
    uv_poll_t mosq_poll;
    int mosq_pevents;
    uv_timer_t timer;
};

static struct dm_platform _plat = {0};

struct dm__topic {
    int mid;
    const struct dm_topic *self;
    struct hr_list_head entry;
    // period topic, auto publish
    uv_timer_t *timer;
};

static HR_LIST_HEAD(_topic_list);

static void _mosquitto_reconnect_timer_cb(uv_timer_t *handle);
static void _topic_period_timer_cb(uv_timer_t *handle) {
    if (!handle || !handle->data) return;
    struct dm__topic *t = (struct dm__topic *)handle->data;

    HR_LOGD("%s(%d): publish topic: %s ...\n", __FUNCTION__, __LINE__, t->self->name);

    // public topics
    void *payload = NULL;
    int len = 0;
    t->self->callback.on_publish(&payload, &len);
    if (payload != NULL && len > 0) {
        int ret = mosquitto_publish(_plat.mosq, &t->mid, t->self->topic,
                                    len, (const void *)payload,
                                    0, false);
        free(payload);
    }
}

static void dm__topic_timer_start(struct dm__topic *t) {
    if (!t || !t->self) return;
    if (t->timer != NULL) {
        uv_timer_start(t->timer, _topic_period_timer_cb, t->self->period, t->self->period);
    }
}
static void dm__topic_timer_stop(struct dm__topic *t) {
    if (!t || !t->self) return;
    if (t->timer != NULL) {
        uv_timer_stop(t->timer);
    }
}
static struct dm__topic *dm__topic_new(const struct dm_topic *topic) {
    struct dm__topic *t = NULL;
    if (!topic)
        return NULL;

    t = (struct dm__topic *)calloc(1, sizeof(struct dm__topic));
    if (!t) {
        return NULL;
    }

    t->self = topic;

    if (t->self->type == TOPIC_TYPE_PUBLISH && t->self->period > 0) {
        t->timer = (uv_timer_t *)calloc(1, sizeof(uv_timer_t));
        uv_timer_init(_plat.loop, t->timer);
        t->timer->data = t;
    }
    HR_INIT_LIST_HEAD(&t->entry);

    hr_list_add_tail(&t->entry, &_topic_list);

    return t;
}

void _close_uv_dynamic_handle(uv_handle_t *handle) {
    HR_LOGD("%s(%d): close :%p\n", __FUNCTION__, __LINE__, handle);
    free(handle);
}

static void dm__topic_free(struct dm__topic *t) {
    if (!t) {
        return;
    }

    // no lock ...
    hr_list_del(&t->entry);

    HR_INIT_LIST_HEAD(&t->entry);

    if (t->timer != NULL) {
        HR_LOGD("%s(%d): close :%p\n", __FUNCTION__, __LINE__, t->timer);
        uv_close((uv_handle_t *)t->timer, _close_uv_dynamic_handle);
    }
    memset((void *)t, 0, sizeof(struct dm__topic));
    free(t);
}

// you must free the pointer
static size_t _read_file(const char *path, char **buf) {
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

    data = (char *)malloc(sb.st_size);
    if (!data) {
        close(fd);
        return -1;
    }
    memset((void *)data, 0, sb.st_size);

    ptr = data;

    size_t remaining = sb.st_size;
    while (remaining > 0) {
        ssize_t n = TEMP_FAILURE_RETRY(read(fd, ptr, remaining));
        if (n <= 0) {
            free(ptr);
            return -1;
        }
        ptr += n;
        remaining -= n;
    }
    close(fd);

    *buf = data;
    return sb.st_size;
}

static ssize_t _write_file(const char *path, const char *data, size_t size) {
    int fd = -1;
    ssize_t left = size;
    const char *ptr = data;

    if (!data || !path || size <= 0) {
        return -1;
    }

    fd = open(path, O_RDWR | O_TRUNC | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd < 0) {
        return -1;
    }

    while (left > 0) {
        ssize_t n = TEMP_FAILURE_RETRY(write(fd, ptr, left));
        if (n == -1) {
            return size - left;
        }
        ptr += n;
        left -= n;
    }

    fdatasync(fd);
    close(fd);

    return size;
}

static void _on_log(struct mosquitto *mosq, void *obj, int level,
                    const char *str) {
    (void)mosq;
    (void)obj;
    (void)level;
    HR_LOGD("mqtt %s\n", str);
}

// https://docs.oasis-open.org/mqtt/mqtt/v3.1.1/errata01/os/mqtt-v3.1.1-errata01-os-complete.html#_Table_3.1_-
static void _on_connect(struct mosquitto *mosq, void *obj, int reason) {
    struct dm_platform *plat = (struct dm_platform *)obj;

    HR_LOGD("%s(%d): reason :%d\n", __FUNCTION__, __LINE__, reason);

    if (CONNACK_ACCEPTED == reason) {
        HR_LOGD("%s(%d): connected, ...\n", __FUNCTION__, __LINE__);
        // auto subscribe all topics
        struct dm__topic *p = NULL, *msg = NULL;
        hr_list_for_each_entry(p, &_topic_list, entry) {
            if (p->self->type == TOPIC_TYPE_SUBSCRIBE) {
                int ret = mosquitto_subscribe(mosq, &p->mid, p->self->topic, 0);
                HR_LOGD("%s(%d): connected, auto subscribe:%s -> (%d)\n", __FUNCTION__, __LINE__, p->self->topic, ret);
            } else {
                // public topics
                void *payload = NULL;
                int len = 0;
                p->self->callback.on_publish(&payload, &len);
                if (payload != NULL && len > 0) {
                    int ret = mosquitto_publish(mosq, &p->mid, p->self->topic,
                                                len, (const void *)payload,
                                                0, false);
                    HR_LOGD("%s(%d): connected, auto publish:%s -> (%d)\n", __FUNCTION__, __LINE__, p->self->topic, ret);
                    free(payload);
                }

                if (p->timer != NULL) {
                    dm__topic_timer_start(p);
                }
            }
        }

        {
            struct sockaddr_in addr;
            socklen_t addr_len = sizeof(addr);
            getsockname(mosquitto_socket(mosq), (struct sockaddr *)&addr, &addr_len);

            // 转换IP和端口
            char ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &addr.sin_addr, ip_str, sizeof(ip_str));
            uint16_t port = ntohs(addr.sin_port);

            HR_LOGD("Local IP: %s, Port: %d\n", ip_str, port);
        }
    } else {
        HR_LOGD("Connection error: %s\n", mosquitto_connack_string(reason));
        mosquitto_disconnect(mosq);
    }
}

static void _on_disconnect(struct mosquitto *mosq, void *userdata, int rc) {
    (void)mosq;
    (void)userdata;
    (void)rc;
    HR_LOGD("%s(%d): \n", __FUNCTION__, __LINE__);

    struct dm_platform *plat = (struct dm_platform *)userdata;
}

static void _on_subscribe(struct mosquitto *mosq, void *obj, int mid,
                          int qos_count, const int *granted_qos) {
    int i;
    bool some_sub_allowed = (granted_qos[0] < 128);
    bool should_print = 1;

    struct dm_platform *plat = (struct dm_platform *)obj;

    if (!plat) return;

    if (should_print)
        printf("Subscribed (mid: %d): %d", mid, granted_qos[0]);
    for (i = 1; i < qos_count; i++) {
        if (should_print)
            printf(", %d", granted_qos[i]);
        some_sub_allowed |= (granted_qos[i] < 128);
    }
    if (should_print)
        printf("\n");

    if (some_sub_allowed == false) {
        mosquitto_disconnect(mosq);
        HR_LOGD("All subscription requests were denied.\n");
    }
}

static void _on_message(struct mosquitto *mosq, void *obj,
                        const struct mosquitto_message *message) {
    (void)mosq;
    const char *method = NULL;
    uint32_t id = 1;
    struct dm_platform *plat = (struct dm_platform *)obj;

    HR_LOGD("%s(%d): receive topic:%s, payloadlen:%d\n", __FUNCTION__, __LINE__,
            message->topic, message->payloadlen);
    if (message->payload) {
        HR_LOGD("%s(%d): receive topic:%s, payloadlen:%d\n%s\n", __FUNCTION__,
                __LINE__, message->topic, message->payloadlen, message->payload);
    }

    if (!message->payload)
        return;

    struct dm__topic *p = NULL;
    hr_list_for_each_entry(p, &_topic_list, entry) {
        // ignore publish response message
        if (p->self->type != TOPIC_TYPE_SUBSCRIBE) {
            continue;
        }

        if (0 != strncmp(message->topic, p->self->topic, strlen(p->self->topic))) {
            continue;
        }

        if (p->self->callback.on_message)
            p->self->callback.on_message(message->payload, message->payloadlen);

        break;
    }
}

static void _on_publish(struct mosquitto *mosq, void *userdata, int mid) {
    HR_LOGD("%s(%d): \n", __FUNCTION__, __LINE__);
}

static void _signal_action(int signum, siginfo_t *siginfo, void *sigcontext) {
    (void)sigcontext;

    HR_LOGD("%s(%d): ........signum:%d\n", __FUNCTION__, __LINE__, signum);

    if (SIGUSR1 == signum) {
        mosquitto_disconnect(_plat.mosq);
        uv_stop(_plat.loop);
    }
}
// {
// 	"username": "who",
// 	"password": "me",
// 	"id": "1234567890",
// 	"server": "127.0.0.1",
// 	"port": 1883
// }
static int _load_conf(struct dm_platform *plat) {
    size_t len = 0;
    char *data = NULL;
    cJSON *root = NULL;

    const char *path = DM_DEFAULT_CONFIG_DIR "/" DM_DEFAULT_CONFIG_NAME;
    if (!plat) return -1;

    len = _read_file(path, &data);
    if (!data) {
        return -1;
    }

    // unlink(path);

    root = cJSON_ParseWithLength(data, len);
    free(data);
    data = NULL;

    if (!root) return -1;

    const char *username = cJSON_GetStringValue(cJSON_GetObjectItem(root, "username"));
    const char *password = cJSON_GetStringValue(cJSON_GetObjectItem(root, "password"));
    const char *client_id = cJSON_GetStringValue(cJSON_GetObjectItem(root, "id"));
    const char *broker = cJSON_GetStringValue(cJSON_GetObjectItem(root, "server"));
    double port = cJSON_GetNumberValue(cJSON_GetObjectItem(root, "port"));

    if (isnan((double)port)) {
        port = BROKER_DEFAULT_PORT;
    } else {
        _plat.port = (int)port;
    }

    HR_LOGD("broker:%s, client_id:%s, username:%s, password:%s\n", broker, client_id, username, password);

    snprintf(plat->username, sizeof(plat->username), "%s", username);
    snprintf(plat->password, sizeof(plat->password), "%s", password);
    snprintf(plat->host, sizeof(plat->host), "%s", broker);
    snprintf(plat->client_id, sizeof(plat->client_id), "%s", client_id);
    cJSON_Delete(root);

    return 0;
}

static void _mosquitto_loop_poll_cb(uv_poll_t *handle, int status, int events) {
    HR_LOGD("%s(%d): come in .....status:%d, event:0x%X..\n", __FUNCTION__, __LINE__, status, events);

    int new_events = DEFAULT_POLL_EVENTS;

    struct dm_platform *plat = (struct dm_platform *)handle->data;
    struct mosquitto *mosq = plat->mosq;

    if (events & UV_READABLE) {
        mosquitto_loop_read(plat->mosq, 1);
    }

    // default no UV_WRITABLE, only care it when mosquitto_want_write
    // so no need add condition : mosquitto_want_write
    if (events & UV_WRITABLE) {
        mosquitto_loop_write(plat->mosq, 1);
    }

    int pevents = plat->mosq_pevents;
    if (mosquitto_want_write(mosq)) {
        if (!(pevents & UV_WRITABLE)) {
            pevents |= UV_WRITABLE;
        }
    } else {
        if (pevents & UV_WRITABLE) {
            pevents ^= UV_WRITABLE;
        }
    }

    if (events & UV_DISCONNECT) {
        HR_LOGD("%s(%d): come in disconnect.......\n", __FUNCTION__, __LINE__);
        // stop current poll, we should reconnect and using new socket
        uv_poll_stop(handle);

        // stop & start reconnect timer callback
        uv_timer_stop(&plat->timer);
        uv_timer_start(&plat->timer, _mosquitto_reconnect_timer_cb, 1000, 1000);

        return;
    }

    if (pevents != plat->mosq_pevents) {
        plat->mosq_pevents = pevents;
        uv_poll_start(&_plat.mosq_poll, pevents, _mosquitto_loop_poll_cb);
    }
}

static void _mosquitto_loop_misc_timer_cb(uv_timer_t *handle) {
    struct dm_platform *plat = (struct dm_platform *)handle->data;

    // mosquitto_loop_misc(plat->mosq);
}

static void _mosquitto_reconnect_timer_cb(uv_timer_t *handle) {
    if (MOSQ_ERR_SUCCESS != mosquitto_reconnect_async(_plat.mosq)) {
        // HR_LOGD("%s(%d): failed reconnect:%s:%d\n", __FUNCTION__, __LINE__, _plat.host, _plat.port);
        return;
    }

    _plat.sock = mosquitto_socket(_plat.mosq);
    // assert(_data.sock != 0);

    // using uv_poll_init update socket
    // any memory leak ?
    uv_poll_init(_plat.loop, &_plat.mosq_poll, _plat.sock);
    _plat.mosq_poll.data = &_plat;
    _plat.mosq_pevents = DEFAULT_POLL_EVENTS;
    uv_poll_start(&_plat.mosq_poll, _plat.mosq_pevents, _mosquitto_loop_poll_cb);

    uv_timer_stop(handle);
    uv_timer_start(&_plat.timer, _mosquitto_loop_misc_timer_cb, 1000, 1000);
}

/* Fully close a loop */
static void close_walk_cb(uv_handle_t *handle, void *arg) {
    if (!uv_is_closing(handle))
        uv_close(handle, NULL);
}

static void close_loop(uv_loop_t *loop) {
    uv_walk(loop, close_walk_cb, NULL);
    uv_run(loop, UV_RUN_DEFAULT);
}
#define MAKE_VALGRIND_HAPPY(loop)         \
    do {                                  \
        close_loop(loop);                 \
        assert(0 == uv_loop_close(loop)); \
        uv_library_shutdown();            \
    } while (0)

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    int rc = 0;
    int retries = 0;

    struct sigaction action;
    memset(&action, 0, sizeof(action));
    sigemptyset(&action.sa_mask);
    action.sa_sigaction = _signal_action;
    sigaction(SIGUSR1, &action, NULL);

    memset((void *)&_plat, 0, sizeof(_plat));

    _plat.qos = 0;

    _plat.alive_time = 60;
    _plat.port = BROKER_DEFAULT_PORT;

    _plat.sock = -1;

    _load_conf(&_plat);

    _plat.loop = uv_default_loop();

    dm_topic_init();

    mosquitto_lib_init();

    _plat.mosq = mosquitto_new(_plat.client_id, false, &_plat);
    if (!_plat.mosq) {
        HR_LOGD("%s(%d): error can not instance mosquitto\n", __FUNCTION__,
                __LINE__);
        return -1;
    }

    mosquitto_log_callback_set(_plat.mosq, _on_log);

    mosquitto_subscribe_callback_set(_plat.mosq, _on_subscribe);
    mosquitto_connect_callback_set(_plat.mosq, _on_connect);
    // mosquitto_connect_with_flags_callback_set(_data.mosq,
    // _on_connect_with_flags);
    mosquitto_disconnect_callback_set(_plat.mosq, _on_disconnect);
    mosquitto_message_callback_set(_plat.mosq, _on_message);
    mosquitto_publish_callback_set(_plat.mosq, _on_publish);
    mosquitto_tls_opts_set(_plat.mosq, 0 /*SSL_VERIFY_NONE*/, NULL, NULL);

    // const char *cafile = "/home/alex/workspace/workspace/libuv/mqtt_cacert.pem";
    // mosquitto_tls_set(_data.mosq, cafile, NULL, NULL, NULL, NULL);
    mosquitto_tls_insecure_set(_plat.mosq, 0);
    mosquitto_tls_opts_set(_plat.mosq, 0, NULL, NULL);

    mosquitto_username_pw_set(_plat.mosq, _plat.username, _plat.password);

    do {
        HR_LOGD("%s(%d): connect:%s:%d\n", __FUNCTION__, __LINE__, _plat.host, _plat.port);
        rc = mosquitto_connect_bind_async(_plat.mosq, _plat.host, _plat.port,
                                          _plat.alive_time, NULL);
        if (rc != MOSQ_ERR_SUCCESS)
            usleep(1000 * 1000);
    } while (rc != MOSQ_ERR_SUCCESS);

    _plat.sock = mosquitto_socket(_plat.mosq);
    // assert(_data.sock != 0);
    HR_LOGD("new sock:%d\n", _plat.sock);

    uv_poll_init(_plat.loop, &_plat.mosq_poll, _plat.sock);
    _plat.mosq_poll.data = &_plat;
    _plat.mosq_pevents = DEFAULT_POLL_EVENTS;
    uv_poll_start(&_plat.mosq_poll, _plat.mosq_pevents, _mosquitto_loop_poll_cb);

    uv_timer_init(_plat.loop, &_plat.timer);
    _plat.timer.data = &_plat;
    uv_timer_start(&_plat.timer, _mosquitto_loop_misc_timer_cb, 1000, 1000);

    _plat.mosq_have_connected = 1;

    uv_run(_plat.loop, UV_RUN_DEFAULT);

    uv_poll_stop(&_plat.mosq_poll);
    uv_timer_stop(&_plat.timer);

    mosquitto_destroy(_plat.mosq);
    _plat.mosq = NULL;
    mosquitto_lib_cleanup();

    {
        struct dm__topic *n, *p;
        hr_list_for_each_entry_safe(p, n, &_topic_list, entry) {
            dm__topic_free(p);
        }
    }

    uv_close((uv_handle_t *)&_plat.mosq_poll, NULL);
    uv_close((uv_handle_t *)&_plat.timer, NULL);

    uv_loop_close(_plat.loop);
    MAKE_VALGRIND_HAPPY(_plat.loop);
    return 0;
}

// implement dm_topic.h
int dm_topic_register(const struct dm_topic *topic) {
    struct dm__topic *t = dm__topic_new(topic);

    return (t != NULL) ? 0 : -1;
}
