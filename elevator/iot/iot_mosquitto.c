#include "iot_mosquitto.h"

#include <mosquitto.h>
#include <mqtt_protocol.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <uv.h>

#include "hr_list.h"
#include "hr_log.h"
#include "iot_topic.h"

#define DEFAULT_POLL_EVENTS (UV_READABLE | UV_DISCONNECT) /*| UV_WRITABLE*/

struct iot_mosquitto {
    struct iot self;
    bool auto_reconnect;
    int sock;  // mosquitto socket
    struct mosquitto* mosq;

    uv_loop_t* loop;
    uv_timer_t timer;
    uv_poll_t poll;

    int pevents;

    struct hr_list_head topic_head;
};

struct iot__topic {
    struct iot_mosquitto* iot;  // current iot
    int mid;
    const struct iot_topic* self;
    struct hr_list_head entry;
    // period topic, auto publish
    uv_timer_t* timer;
    uv_async_t* async;
};

static void iot_mosquitto_loop_misc_timer_cb(uv_timer_t* handle);
static void iot_mosquitto_reconnect_timer_cb(uv_timer_t* handle);
static void iot_mosquitto_loop_poll_cb(uv_poll_t* handle, int status, int events);

static void _topic_period_timer_cb(uv_timer_t* handle) {
    if (!handle || !handle->data)
        return;
    struct iot__topic* t = (struct iot__topic*)handle->data;

    HR_LOGD("%s(%d): publish topic: %s ...\n", __FUNCTION__, __LINE__, t->self->name);

    // public topics
    void* payload = NULL;
    int len = 0;
    t->self->callback.on_publish(&payload, &len);
    if (payload != NULL && len > 0) {
        int rc = mosquitto_publish(t->iot->mosq, &t->mid, t->self->topic,
                                   len, (const void*)payload,
                                   0, false);
        if (rc != MOSQ_ERR_SUCCESS) {
            HR_LOGE("publish failed :%d\n", rc);
        }
        free(payload);
    }
}

static void iot__topic_timer_start(struct iot__topic* t) {
    if (!t || !t->self)
        return;
    if (t->timer != NULL) {
        printf("%s(%d): period:%d\n", __FUNCTION__, __LINE__, t->self->period);
        uv_timer_start(t->timer, _topic_period_timer_cb, t->self->period, t->self->period);
    }
}
static void iot__topic_timer_stop(struct iot__topic* t) {
    if (!t || !t->self)
        return;
    if (t->timer != NULL) {
        uv_timer_stop(t->timer);
    }
}

static void iot__topic_async_cb(uv_async_t* handle) {
    struct iot__topic* t = NULL;
    if (!handle || !handle->data)
        return;

    t = (struct iot__topic*)handle->data;

    HR_LOGD("%s(%d): publish topic: %s ...\n", __FUNCTION__, __LINE__, t->self->name);

    // public topics
    void* payload = NULL;
    int len = 0;
    t->self->callback.on_publish(&payload, &len);
    if (payload != NULL && len > 0) {
        int rc = mosquitto_publish(t->iot->mosq, &t->mid, t->self->topic,
                                   len, (const void*)payload,
                                   0, false);
        if (rc != MOSQ_ERR_SUCCESS) {
            HR_LOGE("publish failed :%d\n", rc);
        }
        free(payload);
    }
}

static void _on_log(struct mosquitto* mosq, void* obj, int level, const char* str) {
    (void)mosq;
    (void)obj;
    (void)level;
    (void)str;
    // HR_LOGD("MQTT %s\n", str);
}

// https://docs.oasis-open.org/mqtt/mqtt/v3.1.1/errata01/os/mqtt-v3.1.1-errata01-os-complete.html#_Table_3.1_-
static void _on_connect(struct mosquitto* mosq, void* obj, int reason) {
    HR_LOGD("%s(%d): reason :%d\n", __FUNCTION__, __LINE__, reason);
    struct iot_mosquitto* iot = (struct iot_mosquitto*)obj;
    if (!mosq || !iot)
        return;

    HR_LOGD("%s(%d): reason :%d\n", __FUNCTION__, __LINE__, reason);
    /*if (iot->is_dynamic_register) {
        HR_LOGD("%s(%d): register connect reason :%d\n", __FUNCTION__, __LINE__, reason);
        return;
    }*/

    if (CONNACK_ACCEPTED == reason) {
        HR_LOGD("%s(%d): connected, ...\n", __FUNCTION__, __LINE__);
        // auto subscribe all topics
        struct iot__topic* p = NULL;
        hr_list_for_each_entry(p, &iot->topic_head, entry) {
            HR_LOGD("%s(%d): topic %s...\n", __FUNCTION__, __LINE__, p->self->topic);
            if (p->self->type == TOPIC_TYPE_SUBSCRIBE) {
                int ret = mosquitto_subscribe(mosq, &p->mid, p->self->topic, 0);
                HR_LOGD("%s(%d): connected, auto subscribe:%s -> (%d)\n", __FUNCTION__, __LINE__, p->self->topic, ret);
            } else {
                // public topics
                void* payload = NULL;
                int len = 0;
                if (p->self->auto_public != 0) {
                    p->self->callback.on_publish(&payload, &len);
                    if (payload != NULL && len > 0) {
                        int rc = mosquitto_publish(mosq, &p->mid, p->self->topic,
                                                   len, (const void*)payload,
                                                   0, false);
                        if (rc != MOSQ_ERR_SUCCESS) {
                            HR_LOGE("publish failed :%d\n", rc);
                        }
                        HR_LOGD("%s(%d): connected, auto publish:%s -> (%d)\n", __FUNCTION__, __LINE__, p->self->topic, rc);
                        free(payload);
                    }
                }
#if 0  // move to iot_mosquitto_run
       // create topic timer delay when it's connected
                if (p->self->type == TOPIC_TYPE_PUBLISH && p->self->period > 0 && !p->timer) {
                    p->timer = (uv_timer_t*)calloc(1, sizeof(uv_timer_t));
                    uv_timer_init(iot->poll.loop, p->timer);
                    p->timer->data = p;
                }
                if (p->self->type == TOPIC_TYPE_PUBLISH && !p->async) {
                    p->async = (uv_async_t*)calloc(1, sizeof(uv_timer_t));
                    p->async->data = p;
                    uv_async_init(iot->poll.loop, p->async, iot__topic_async_cb);
                }
#endif
                if (p->timer != NULL) {
                    iot__topic_timer_start(p);
                }
            }
        }
    } else {
        HR_LOGD("Connection error: %s\n", mosquitto_connack_string(reason));
        mosquitto_disconnect(mosq);
    }
}

static void _on_disconnect(struct mosquitto* mosq, void* userdata, int rc) {
    (void)mosq;
    (void)userdata;
    (void)rc;
    HR_LOGD("%s(%d): \n", __FUNCTION__, __LINE__);

    struct iot_mosquitto* iot = (struct iot_mosquitto*)userdata;
    if (!mosq || !iot)
        return;
}

static void _on_subscribe(struct mosquitto* mosq, void* obj, int mid, int qos_count, const int* granted_qos) {
    int i;
    bool some_sub_allowed = (granted_qos[0] < 128);
    bool should_print = 1;

    struct iot_mosquitto* iot = (struct iot_mosquitto*)obj;
    if (!mosq || !iot)
        return;

    if (should_print)
        HR_LOGD("Subscribed (mid: %d): %d", mid, granted_qos[0]);
    for (i = 1; i < qos_count; i++) {
        if (should_print)
            HR_LOGD(", %d", granted_qos[i]);
        some_sub_allowed |= (granted_qos[i] < 128);
    }
    if (should_print)
        HR_LOGD("\n");

    if (some_sub_allowed == false) {
        mosquitto_disconnect(mosq);
        HR_LOGD("All subscription requests were denied.\n");
    }
}

static void _on_message(struct mosquitto* mosq, void* obj, const struct mosquitto_message* message) {
    (void)mosq;
    struct iot_mosquitto* iot = (struct iot_mosquitto*)obj;
    if (!mosq || !iot)
        return;
    HR_LOGD("%s(%d): receive topic:%s, payloadlen:%d\n", __FUNCTION__, __LINE__,
            message->topic, message->payloadlen);
    if (message->payload) {
        HR_LOGD("%s(%d): receive topic:%s, payloadlen:%d\n%s\n", __FUNCTION__,
                __LINE__, message->topic, message->payloadlen, message->payload);
    }

    if (!message->payload)
        return;

    struct iot__topic* p = NULL;
    hr_list_for_each_entry(p, &iot->topic_head, entry) {
        // ignore publish response message
        if (p->self->type != TOPIC_TYPE_SUBSCRIBE) {
            continue;
        }

        if (0 != strncmp(message->topic, p->self->topic, strlen(p->self->topic))) {
            continue;
        }

        if (p->self->callback.on_message) {
            p->self->callback.on_message(message->payload, message->payloadlen);
        }

        break;
    }
}

static void _on_publish(struct mosquitto* mosq, void* userdata, int mid) {
    (void)mosq;
    (void)userdata;
    (void)mid;
    HR_LOGD("%s(%d): \n", __FUNCTION__, __LINE__);
}

static void iot_mosquitto_loop_misc_timer_cb(uv_timer_t* handle) {
    struct iot_mosquitto* iot = (struct iot_mosquitto*)handle->data;

    // check is connected?
    mosquitto_loop_misc(iot->mosq);
}

static void iot_mosquitto_reconnect_timer_cb(uv_timer_t* handle) {
    struct iot_mosquitto* iot = NULL;
    struct mosquitto* mosq = NULL;

    HR_LOGD("%s(%d): \n", __FUNCTION__, __LINE__);
    if (!handle || !handle->data)
        return;

    iot = (struct iot_mosquitto*)handle->data;

    mosq = iot->mosq;

    if (!mosq)
        return;

    printf("%s(%d): mosq:%p\n", __FUNCTION__, __LINE__, (void*)mosq);
    if (MOSQ_ERR_SUCCESS != mosquitto_reconnect_async(mosq)) {
        HR_LOGD("%s(%d): failed reconnect\n", __FUNCTION__, __LINE__);
        return;
    }

    iot->sock = mosquitto_socket(mosq);

    // using uv_poll_init update socket
    // any memory leak ?
    uv_poll_init(iot->poll.loop, &iot->poll, iot->sock);
    iot->poll.data = iot;
    iot->pevents = DEFAULT_POLL_EVENTS;
    uv_poll_start(&iot->poll, iot->pevents, iot_mosquitto_loop_poll_cb);

    uv_timer_stop(handle);
    uv_timer_start(&iot->timer, iot_mosquitto_loop_misc_timer_cb, 1000, 1000);
}

static void iot_mosquitto_loop_poll_cb(uv_poll_t* handle, int status, int events) {
    HR_LOGD("%s(%d): come in .....status:%d, event:0x%X..\n", __FUNCTION__, __LINE__, status, events);

    // struct dm_platform *plat = (struct dm_platform *)handle->data;
    struct iot_mosquitto* iot = NULL;
    struct mosquitto* mosq = NULL;

    if (!handle || !handle->data)
        return;

    iot = (struct iot_mosquitto*)handle->data;

    mosq = iot->mosq;

    if (!mosq)
        return;

    if (events & UV_READABLE) {
        mosquitto_loop_read(mosq, 1);
    }

    // default no UV_WRITABLE, only care it when mosquitto_want_write
    // so no need add condition : mosquitto_want_write
    if (events & UV_WRITABLE) {
        mosquitto_loop_write(mosq, 1);
    }

    int pevents = iot->pevents;
    if (mosquitto_want_write(mosq)) {
        if (!(pevents & UV_WRITABLE)) {
            pevents |= UV_WRITABLE;
        }
    } else {
        if (pevents & UV_WRITABLE) {
            pevents ^= UV_WRITABLE;
        }
    }

#if 1
    if (events & UV_DISCONNECT) {
        HR_LOGD("%s(%d): come in disconnect.......\n", __FUNCTION__, __LINE__);
        // stop current poll, we should reconnect and using new socket
        uv_poll_stop(handle);

        if (iot->auto_reconnect) {
            // stop & start reconnect timer callback
            uv_timer_stop(&iot->timer);
            uv_timer_start(&iot->timer, iot_mosquitto_reconnect_timer_cb, 1000, 1000);
        }

        return;
    }
#endif

    if (pevents != iot->pevents) {
        iot->pevents = pevents;
        uv_poll_start(&iot->poll, pevents, iot_mosquitto_loop_poll_cb);
    }
}

static struct iot__topic* iot__topic_new(const struct iot_topic* topic) {
    struct iot__topic* t = NULL;
    if (!topic)
        return NULL;

    t = (struct iot__topic*)calloc(1, sizeof(struct iot__topic));
    if (!t) {
        return NULL;
    }

    t->self = topic;
#if 0  // init timer later, because now maybe no loop
    if (t->self->type == TOPIC_TYPE_PUBLISH && t->self->period > 0) {
        t->timer = (uv_timer_t*)calloc(1, sizeof(uv_timer_t));
        uv_timer_init(iot->poll.loop, t->timer);
        t->timer->data = t;
    }
#endif
    HR_INIT_LIST_HEAD(&t->entry);

    return t;
}

static void _close_uv_dynamic_handle(uv_handle_t* handle) {
    HR_LOGD("%s(%d): close :%p\n", __FUNCTION__, __LINE__, handle);
    free(handle);
}
static void iot__topic_free(struct iot__topic* t) {
    if (!t) {
        return;
    }

    // no lock ...
    hr_list_del(&t->entry);

    HR_INIT_LIST_HEAD(&t->entry);

    if (t->timer != NULL) {
        HR_LOGD("%s(%d): close :%p\n", __FUNCTION__, __LINE__, t->timer);
        printf("%s(%d): close :%p\n", __FUNCTION__, __LINE__, t->timer);
        uv_close((uv_handle_t*)t->timer, _close_uv_dynamic_handle);
        t->timer = NULL;
    }
    if (t->async) {
        uv_close((uv_handle_t*)t->async, _close_uv_dynamic_handle);
        t->async = NULL;
    }
    memset((void*)t, 0, sizeof(struct iot__topic));
    free(t);
}

struct iot* iot_mosquitto_alloc() {
    struct iot_mosquitto* iot = (struct iot_mosquitto*)calloc(1, sizeof(struct iot_mosquitto));
    if (!iot) {
        printf("%s(%d): ............\n", __FUNCTION__, __LINE__);
        return NULL;
    }

    HR_LOGE("%s(%d): iot:%p iot_mosquitto:%p\n", __FUNCTION__, __LINE__, &iot->self, iot);
    HR_INIT_LIST_HEAD(&iot->topic_head);

    return &iot->self;
}
int iot_mosquitto_release(struct iot* self) {
    uv_loop_t* loop = NULL;
    struct iot__topic *n = NULL, *p = NULL;
    struct iot_mosquitto* iot = container_of(self, struct iot_mosquitto, self);
    if (!self || !iot) {
        printf("%s(%d): ............\n", __FUNCTION__, __LINE__);
        return -1;
    }

    HR_LOGE("%s(%d): iot:%p iot_mosquitto:%p\n", __FUNCTION__, __LINE__, &iot->self, iot);

    loop = iot->poll.loop;
    uv_poll_stop(&iot->poll);
    uv_timer_stop(&iot->timer);

    uv_close((uv_handle_t*)&iot->poll, NULL);
    uv_close((uv_handle_t*)&iot->timer, NULL);

    mosquitto_disconnect(iot->mosq);

    mosquitto_destroy(iot->mosq);

    if (self->id) {
        free(self->id);
        self->id = NULL;
    }
    if (self->server) {
        free(self->server);
        self->server = NULL;
    }
    if (self->username) {
        free(self->username);
        self->username = NULL;
    }
    if (self->password) {
        free(self->password);
        self->password = NULL;
    }

    hr_list_for_each_entry_safe(p, n, &iot->topic_head, entry) {
        iot__topic_free(p);
    }

    HR_INIT_LIST_HEAD(&iot->topic_head);

    // fixed valgrind memory problem
    // free memory after loop
    uv_run(loop, UV_RUN_DEFAULT);
    // must free after loop finished
    free(iot);
    return 0;
}

int iot_mosquitto_prepare(struct iot* self) {
    int rc = -1;
    struct mosquitto* mosq = NULL;
    struct iot_mosquitto* iot = container_of(self, struct iot_mosquitto, self);

    HR_LOGE("%s(%d): id:%s\n", __FUNCTION__, __LINE__, self->id);
    if (!self || !iot || !self->id) {
        HR_LOGE("%s(%d): \n", __FUNCTION__, __LINE__);
        return -1;
    }
    if (!iot->mosq) {
        iot->mosq = mosquitto_new(self->id, true, iot);
        if (!iot->mosq) {
            HR_LOGD("%s(%d): error can not instance mosquitto\n", __FUNCTION__,
                    __LINE__);
            return -1;
        }
    } else {
        mosquitto_reinitialise(iot->mosq, self->id, true, iot);
    }

    mosq = iot->mosq;

    HR_LOGE("%s(%d): \n", __FUNCTION__, __LINE__);
    iot->sock = -1;

    iot->auto_reconnect = 1;

    mosquitto_log_callback_set(mosq, _on_log);

    mosquitto_subscribe_callback_set(mosq, _on_subscribe);
    mosquitto_connect_callback_set(mosq, _on_connect);
    // mosquitto_connect_with_flags_callback_set(mosq,
    // _on_connect_with_flags);
    mosquitto_disconnect_callback_set(mosq, _on_disconnect);
    mosquitto_message_callback_set(mosq, _on_message);
    mosquitto_publish_callback_set(mosq, _on_publish);
    mosquitto_tls_opts_set(mosq, 0 /*SSL_VERIFY_NONE*/, NULL, NULL);

    // const char *cafile = "/home/alex/workspace/workspace/libuv/libuv/iot/ali_iot_ca.crt";
    // mosquitto_tls_set(mosq, cafile, NULL, NULL, NULL, NULL);
    // mosquitto_tls_insecure_set(mosq, false);
    //  mosquitto_tls_opts_set(mosq, 0, NULL, NULL);

    mosquitto_username_pw_set(iot->mosq, self->username, self->password);

    HR_LOGE("%s(%d): \n", __FUNCTION__, __LINE__);
    do {
        HR_LOGD("%s(%d): connect:%s:%d\n", __FUNCTION__, __LINE__, self->server, self->port);
        // 我们发现我电脑 apt 安装的 mosquitto 使用异步连接阿里 iot 的时候总是连接不上,但是 sync 接口测试正常
        // 后来使用自己编译的 mosquitto 测试正常
        // rc = mosquitto_connect_bind_async(iot->mosq, _plat.conf.broker.server, _plat.conf.broker.port,
        //_plat.alive_time, NULL);

        rc = mosquitto_connect(iot->mosq, self->server, self->port, 60);
        if (rc != MOSQ_ERR_SUCCESS)
            usleep(1000 * 1000);
    } while (rc != MOSQ_ERR_SUCCESS);

    HR_LOGE("%s(%d): \n", __FUNCTION__, __LINE__);
    iot->sock = mosquitto_socket(iot->mosq);

    HR_LOGE("%s(%d): \n", __FUNCTION__, __LINE__);
    return 0;
}
int iot_mosquitto_run(struct iot* self, uv_loop_t* loop) {
    struct iot__topic* p = NULL;
    struct iot_mosquitto* iot = container_of(self, struct iot_mosquitto, self);
    HR_LOGE("%s(%d): iot:%p iot_mosquitto:%p\n", __FUNCTION__, __LINE__, self, iot);
    if (!self || !iot || !loop)
        return -1;
    // using uv_poll_init update socket
    // any memory leak ?
    uv_poll_init(loop, &iot->poll, iot->sock);
    iot->poll.data = iot;
    iot->pevents = DEFAULT_POLL_EVENTS;
    uv_poll_start(&iot->poll, iot->pevents, iot_mosquitto_loop_poll_cb);

    uv_timer_init(loop, &iot->timer);
    iot->timer.data = iot;
    uv_timer_start(&iot->timer, iot_mosquitto_loop_misc_timer_cb, 1000, 1000);

    // init topics
    hr_list_for_each_entry(p, &iot->topic_head, entry) {
        // create topic timer delay when it's connected
        if (p->self->type == TOPIC_TYPE_PUBLISH && p->self->period > 0 /*&& !p->timer*/) {
            p->timer = (uv_timer_t*)calloc(1, sizeof(uv_timer_t));
            uv_timer_init(iot->poll.loop, p->timer);
            p->timer->data = p;
        }
        if (p->self->type == TOPIC_TYPE_PUBLISH /*&& !p->async*/) {
            p->async = (uv_async_t*)calloc(1, sizeof(uv_async_t));
            p->async->data = p;
            uv_async_init(iot->poll.loop, p->async, iot__topic_async_cb);
        }
    }

    return 0;
}

int iot_mosquitto_topic_register(struct iot* self, const struct iot_topic* topic) {
    struct iot_mosquitto* iot = container_of(self, struct iot_mosquitto, self);
    HR_LOGE("%s(%d): iot:%p topic:%s\n", __FUNCTION__, __LINE__, self, topic->topic);
    struct iot__topic* t = NULL;
    if (!self || !iot || !topic) {
        return -1;
    }

    t = iot__topic_new(topic);
    if (!t) {
        return -1;
    }

    // keep iot reference
    t->iot = iot;
    // attach topic to iot
    hr_list_add_tail(&t->entry, &iot->topic_head);
    return 0;
}

int iot_mosquitto_public_async(struct iot* self, const struct iot_topic* topic) {
    struct iot_mosquitto* iot = container_of(self, struct iot_mosquitto, self);

    if (!self || !iot || !topic) {
        return -1;
    }

    if (topic->type != TOPIC_TYPE_PUBLISH) {
        HR_LOGE("%s(%d): topic is not publish: %s\n", __FUNCTION__, __LINE__, topic->name);
        return -1;
    }
    struct iot__topic* p = NULL;
    hr_list_for_each_entry(p, &iot->topic_head, entry) {
        // ignore publish response message
        if (p->self->type != TOPIC_TYPE_PUBLISH) {
            continue;
        }

        if (p->self == topic) {
            if (!p->async) {
                // connection is not finish, ignore it
                continue;
            }
            uv_async_send(p->async);
            break;
        }
    }

    return 0;
}