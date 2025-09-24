
// mxp, 20250502, a smiple mqtt iot framework based on libuv library
// you should implement your topic only, do not care any other logic
// who to use:
// 1. call uviot_alloc to allocate struct uviot object;(you should pass your uv_loop handle)
// 2. fill information into struct uviot; (such as clientid, server url, port, alive time, username and password)
// 3. using uviot_topic_register to register your topics
// 4. call uviot_prepare connect your mqtt platform and poll to loop, then you can call uv_run
// 5. when you want to publish message, call uviot_publish_async, then you on_publish callback will be called;(uviot_publish_async is thread safe)
// 6. finally call uviot_release to release all resource
// 7. what's more, uviot_get_connection_ipv4/mac_address can be used to obtion mqtt connection information such as ip & mac

#include "uviot.h"

#include <ifaddrs.h>
#include <mosquitto.h>
#include <mqtt_protocol.h>
#include <net/if.h>
#include <resolv.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <uv.h>

#include "hr_list.h"
#include "hr_log.h"

// now reconnect api not using async, it maybe blocked much time
// we should period reconnect, not too quick
#define UVIOT_RECONNECT_DELAY (5000)
// max stash 32 items, must be power of 2
#define STASH_MESSAGE_CAPABILITY (1 << 5)  // 2^5

// 感觉不能主动调用 disconnect， 必须先停止 pool 然后再调用 disconnect

#define DEFAULT_POLL_EVENTS (UV_READABLE | UV_DISCONNECT | UV_WRITABLE)

struct uviot_impl {
    struct uviot self;
    bool auto_reconnect;
    int sock;  // mosquitto socket
    struct mosquitto* mosq;

    uv_loop_t* loop;
    uv_timer_t timer;
    uv_poll_t poll;
    uv_work_t conn_req;
    int connect_method;  // 0: use connect, 1: can use reconnect

    int refs;  // uv handle reference nums

    int pevents;

    struct hr_list_head topic_head;
    struct {
        char ipv4[INET_ADDRSTRLEN];
        char mac[18];
    } status;
};

struct uviot__topic {
    struct uviot_impl* iot;  // current iot
    int mid;
    const struct uviot_topic* self;
    struct hr_list_head entry;
    // period topic, auto publish
    uv_timer_t* timer;
    uv_async_t* async;

    // the quality of important messages must be guaranteed.
    // only stash message you send using uviot_publish_async and qos > 0
    struct {
        unsigned int head;
        unsigned int tail;
        unsigned int caps;  // buffer max size
        const char** data;
    } stash;  // pending messages

    int refs;
};

static int mosquitto_lib_refs = 0;

static void uviot_impl_loop_misc_timer_cb(uv_timer_t* handle);
static void uviot_impl_loop_poll_cb(uv_poll_t* handle, int status, int events);
static void uviot_impl_connect_timer_cb(uv_timer_t* handle);

static void uviot__close_uv_dynamic_handle(uv_handle_t* handle);

// wrapper mosquitto_publish, process error code
static int uviot_mosquitto_publish(struct uviot_impl* iot, int* mid, const char* topic, int payloadlen, const void* payload, int qos, bool retain) {
    if (!iot)
        return -1;

    // maybe not connected
    if (mosquitto_socket(iot->mosq) == -1) {
        return -1;
    }

    int rc = mosquitto_publish(iot->mosq, mid, topic, payloadlen, payload, qos, retain);
#if 0
    // 20250611, only care CONN_LOST, other events maybe trigger disconnect callback
    if (rc == MOSQ_ERR_CONN_LOST) {
        HR_LOGE("publish failed, we reconnect again: rc:%d, errno:%d\n", rc, errno);
         if (!uv_is_closing((uv_handle_t*)&iot->poll)) {
            uv_poll_stop(&iot->poll);
            uv_close((uv_handle_t*)&iot->poll, uviot__close_uv_dynamic_handle);
        }

        uv_timer_stop(&iot->timer);
        // uv_timer_start(&iot->timer, uviot_impl_reconnect_timer_cb, 0, 1000);
        uv_timer_start(&iot->timer, uviot_impl_connect_timer_cb, UVIOT_RECONNECT_DELAY, 0);
    }
#endif
    return rc;
}

static void _topic_period_timer_cb(uv_timer_t* handle) {
    if (!handle || !handle->data)
        return;
    struct uviot__topic* t = (struct uviot__topic*)handle->data;

    if (mosquitto_socket(t->iot->mosq) == -1) {
        return;
    }

    // public topics
    void* payload = NULL;
    int len = 0;
    t->self->callback.on_publish(&payload, &len);
    if (payload != NULL && len > 0) {
        int rc = uviot_mosquitto_publish(t->iot, &t->mid, t->self->topic,
                                         len, (const void*)payload,
                                         t->self->qos, false);
        if (rc != MOSQ_ERR_SUCCESS) {
            HR_LOGE("publish failed :%d\n", rc);
        }
        free(payload);
    }
}

static int iot__topic_stash_push(struct uviot__topic* t, const char* data) {
    int id = 0;
    if (!t || !t->self || !data)
        return -1;

    if (!t->stash.data) {
        // init pending message
        t->stash.caps = STASH_MESSAGE_CAPABILITY;
        // memory will be release when deinit
        t->stash.data = (const char**)calloc(t->stash.caps, sizeof(const char*));
        if (!t->stash.data) {
            t->stash.caps = 0;
            return -1;
        }
    }
    // always put new data overwrite
    id = t->stash.head & (t->stash.caps - 1);

    printf("id: %d (%d - %d)-> %s\n", id, t->stash.head, t->stash.tail, data);
    // free old data
    if (t->stash.data[id]) {
        free((void*)t->stash.data[id]);
        t->stash.tail++;  // tail should be moved
    }
    t->stash.data[id] = data;
    t->stash.head++;

    return 0;
}
#if 0
static int iot__topic_stash_pop(struct uviot__topic* t, const char** data) {
    int id = 0;
    if (!t || !t->self || !t->stash.data || !data)
        return -1;

    // no pending message
    if (t->stash.head - t->stash.tail == 0) {
        return -1;
    }
    // always put new data overwrite
    id = t->stash.tail & (t->stash.caps - 1);

    *data = t->stash.data[id];
    t->stash.tail++;

    return 0;
}
#endif
static void iot__topic_timer_start(struct uviot__topic* t) {
    if (!t || !t->self)
        return;
    if (t->timer != NULL) {
        uv_timer_start(t->timer, _topic_period_timer_cb, t->self->period, t->self->period);
    }
}
static void iot__topic_timer_stop(struct uviot__topic* t) {
    if (!t || !t->self)
        return;
    if (t->timer != NULL) {
        uv_timer_stop(t->timer);
    }
}

static void iot__topic_async_cb(uv_async_t* handle) {
    struct uviot__topic* t = NULL;
    if (!handle || !handle->data)
        return;

    t = (struct uviot__topic*)handle->data;

    // always call on_publish, message maybe stashed when no connect
    // maybe not connected
    // if (mosquitto_socket(t->iot->mosq) == -1) {
    //    return;
    // }

    // public topics
    void* payload = NULL;
    int len = 0;
    t->self->callback.on_publish(&payload, &len);
    if (payload != NULL && len > 0) {
        int rc = uviot_mosquitto_publish(t->iot, &t->mid, t->self->topic,
                                         len, (const void*)payload,
                                         t->self->qos, false);
        printf("publish :%d -> %s\n", rc, (char*)payload);
        if (rc != MOSQ_ERR_SUCCESS) {
            HR_LOGE("publish failed :%d, errno:%d\n", rc, errno);
            if (t->self->qos > 0) {
                HR_LOGE("failed, try stash the message\n");
                // after success, payload will be release later
                if (0 == iot__topic_stash_push(t, payload)) {
                    return;
                }
            }
        }
        free(payload);
    }
}

static void _update_connection_status(struct uviot_impl* iot) {
    struct ifreq ifr;
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);

    if (!iot || iot->sock <= 0)
        return;

    int sock = iot->sock;
    if (sock < 0)
        return;

    if (0 != getsockname(sock, (struct sockaddr*)&addr, &addr_len)) {
        return;
    }

    inet_ntop(AF_INET, &addr.sin_addr, iot->status.ipv4, sizeof(iot->status.ipv4));
    uint16_t port = ntohs(addr.sin_port);

    HR_LOGD("Local IP: %s, Port: %d\n", iot->status.ipv4, port);

    // platform_set_connection_ipv4_address(iot->status.ipv4);

    memset(&ifr, 0, sizeof(ifr));
    ifr.ifr_addr.sa_family = AF_INET;
    if (ioctl(sock, SIOCGIFNAME, &ifr) == -1) {
        perror("ioctl SIOCGIFNAME");
        return;
    }

    if (ioctl(sock, SIOCGIFHWADDR, &ifr) == -1) {
        perror("ioctl SIOCGIFHWADDR");
        return;
    }

    unsigned char* mac = (unsigned char*)ifr.ifr_hwaddr.sa_data;

    snprintf(iot->status.mac, sizeof(iot->status.mac), "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    HR_LOGD("now %s -> %s\n", iot->status.mac, iot->status.ipv4);
    // platform_set_connection_mac_address(iot->status.mac);
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
    struct uviot_impl* iot = (struct uviot_impl*)obj;
    if (!mosq || !iot)
        return;

    HR_LOGD("%s(%d): reason :%d\n", __FUNCTION__, __LINE__, reason);

    if (CONNACK_ACCEPTED == reason) {
        HR_LOGD("%s(%d): connected, ...\n", __FUNCTION__, __LINE__);
        _update_connection_status(iot);

        // auto subscribe all topics
        struct uviot__topic* p = NULL;
        hr_list_for_each_entry(p, &iot->topic_head, entry) {
            HR_LOGD("%s(%d): topic %s...\n", __FUNCTION__, __LINE__, p->self->topic);
            if (p->self->type == TOPIC_TYPE_SUBSCRIBE) {
                int ret = mosquitto_subscribe(mosq, &p->mid, p->self->topic, p->self->qos);
                HR_LOGD("%s(%d): connected, auto subscribe:%s -> (%d)\n", __FUNCTION__, __LINE__, p->self->topic, ret);
            } else {
                // public topics
                void* payload = NULL;
                int len = 0;
                if (p->self->auto_publish != 0) {
                    p->self->callback.on_publish(&payload, &len);
                    if (payload != NULL && len > 0) {
                        int rc = uviot_mosquitto_publish(iot, &p->mid, p->self->topic,
                                                         len, (const void*)payload,
                                                         p->self->qos, false);
                        if (rc != MOSQ_ERR_SUCCESS) {
                            HR_LOGE("publish failed :%d\n", rc);
                        }
                        HR_LOGD("%s(%d): connected, auto publish:%s -> (%d)\n", __FUNCTION__, __LINE__, p->self->topic, rc);
                        free(payload);
                    }
                }

                // our stash message is send before mosquitto message
                if (p->stash.data) {
                    while (p->stash.tail < p->stash.head) {
                        int id = p->stash.tail & (p->stash.caps - 1);
                        if (p->stash.data[id]) {
                            int rc = uviot_mosquitto_publish(iot, &p->mid, p->self->topic,
                                                             strlen(p->stash.data[id]), (const void*)p->stash.data[id],
                                                             p->self->qos, false);
                            HR_LOGD("%s(%d): auto publish stash message:%s -> (%d)\n", __FUNCTION__, __LINE__, p->self->topic, rc);
                            if (rc != MOSQ_ERR_SUCCESS) {
                                HR_LOGD("%s(%d): ignore failed stash message:%s -> (%d)\n", __FUNCTION__, __LINE__, p->self->topic, rc);
                            }

                            free((void*)p->stash.data[id]);
                        }
                        p->stash.tail++;
                    }
                }

#if 0  // move to uviot_impl_run
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

    struct uviot_impl* iot = (struct uviot_impl*)userdata;
    if (!mosq || !iot)
        return;
    HR_LOGD("%s(%d): \n", __FUNCTION__, __LINE__);

    // disconnect maybe trigger in keepalive so we should reconnect
    // but keepalive is in timer callback not poll back
    if (!uv_is_closing((uv_handle_t*)&iot->poll)) {
        uv_poll_stop(&iot->poll);
        uv_close((uv_handle_t*)&iot->poll, uviot__close_uv_dynamic_handle);
    }

    if (iot->auto_reconnect) {
        // stop & start reconnect timer callback
        uv_timer_start(&iot->timer, uviot_impl_connect_timer_cb, UVIOT_RECONNECT_DELAY, 0);
    }
}

static void _on_subscribe(struct mosquitto* mosq, void* obj, int mid, int qos_count, const int* granted_qos) {
    int i;
    bool some_sub_allowed = (granted_qos[0] < 128);
    bool should_print = 1;

    struct uviot_impl* iot = (struct uviot_impl*)obj;
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
    struct uviot_impl* iot = (struct uviot_impl*)obj;
    if (!mosq || !iot)
        return;
#if 0
    HR_LOGD("%s(%d): receive topic:%s, payloadlen:%d\n", __FUNCTION__, __LINE__,
            message->topic, message->payloadlen);
    if (message->payload) {
        HR_LOGD("%s(%d): receive topic:%s, payloadlen:%d\n%s\n", __FUNCTION__,
                __LINE__, message->topic, message->payloadlen, (const char*)message->payload);
    }
#endif

    if (!message->payload)
        return;

    struct uviot__topic* p = NULL;
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
}

static void uviot_impl_loop_misc_timer_cb(uv_timer_t* handle) {
    struct uviot_impl* iot = (struct uviot_impl*)handle->data;
    // check i connected?
    mosquitto_loop_misc(iot->mosq);
}

static void uviot_impl_do_connnect_work(uv_work_t* req) {
    int rc = 0;
    struct uviot_impl* iot = NULL;
    struct mosquitto* mosq = NULL;

    if (!req || !req->data)
        return;

    iot = (struct uviot_impl*)req->data;
    mosq = iot->mosq;
    HR_LOGD("%s(%d): connect:%s:%d, alive time:%d\n", __FUNCTION__, __LINE__, iot->self.server, iot->self.port, iot->self.alive_time);

    // mosquitto_[re]connect_async maybe also blocked when dns resolver
    // we should do it in background thread not in uv main loop
    if (iot->connect_method == 0) {
        rc = mosquitto_connect_async(mosq, iot->self.server, iot->self.port, iot->self.alive_time);
    } else {
        rc = mosquitto_reconnect_async(mosq);
    }
    HR_LOGD("%s(%d): connect:%s:%d, alive time:%d, failed:%d\n", __FUNCTION__, __LINE__, iot->self.server, iot->self.port, iot->self.alive_time, rc);
    if (rc != MOSQ_ERR_SUCCESS) {
        HR_LOGD("%s(%d): connect:%s:%d, alive time:%d, failed:%d\n", __FUNCTION__, __LINE__, iot->self.server, iot->self.port, iot->self.alive_time, rc);
        if (rc == MOSQ_ERR_EAI) {
            res_init();
        }
        return;  // auto connect retry
    }

    iot->sock = mosquitto_socket(iot->mosq);
#if 0
    if (iot->sock != -1) {
        int opt;
        /* Set non-blocking */
        opt = fcntl(iot->sock, F_GETFL, 0);

        if (opt == -1 || fcntl(iot->sock, F_SETFL, opt | O_NONBLOCK) == -1) {
            /* If either fcntl fails, don't want to allow this client to connect. */
            iot->sock = -1;
            return;
        } else {
            printf("non block success\n");
        }
    }
#endif
}

static void uviot_impl_after_connect_work(uv_work_t* req, int status) {
    struct uviot* self = NULL;
    struct uviot_impl* iot = NULL;
    struct mosquitto* mosq = NULL;

    if (!req || !req->data) {
        return;
    }

    iot = (struct uviot_impl*)req->data;
    self = &iot->self;
    mosq = iot->mosq;

    iot->refs--;

    if (status == UV_ECANCELED) {
        printf("it's canceled\n");
        return;
    }


    if (iot->sock == -1) {
        HR_LOGD("%s(%d): connect failed, schedule timer again...\n", __FUNCTION__, __LINE__);
        uv_timer_start(&iot->timer, uviot_impl_connect_timer_cb, UVIOT_RECONNECT_DELAY, 0);
        return;
    }

    if (iot->connect_method == 0) {
        // next connect will use reconnect
        iot->connect_method = 1;
    }
    HR_LOGD("%s(%d): connected ...\n", __FUNCTION__, __LINE__);
    // using uv_poll_init update socket
    // any memory leak ?
    uv_poll_init(iot->loop, &iot->poll, iot->sock);
    iot->poll.data = iot;
    iot->pevents = DEFAULT_POLL_EVENTS;
    uv_poll_start(&iot->poll, iot->pevents, uviot_impl_loop_poll_cb);
    iot->refs++;

    // switch to misc timer
    uv_timer_start(&iot->timer, uviot_impl_loop_misc_timer_cb, self->alive_time * 1000, self->alive_time * 1000);
}

static void uviot_impl_connect_timer_cb(uv_timer_t* handle) {
    struct uviot_impl* iot = NULL;
    struct mosquitto* mosq = NULL;

    HR_LOGD("%s(%d): \n", __FUNCTION__, __LINE__);
    if (!handle || !handle->data)
        return;

    iot = (struct uviot_impl*)handle->data;

    mosq = iot->mosq;

    if (!mosq)
        return;

    // make sure it's closed
    if (uv_has_ref((uv_handle_t*)&iot->poll)) {
        if (!uv_is_closing((uv_handle_t*)&iot->poll)) {
            uv_poll_stop(&iot->poll);
            uv_close((uv_handle_t*)&iot->poll, uviot__close_uv_dynamic_handle);
        }
    }

    iot->sock = -1;
    iot->conn_req.data = iot;

    iot->refs++;
    uv_queue_work(iot->loop, &iot->conn_req, uviot_impl_do_connnect_work, uviot_impl_after_connect_work);
    HR_LOGD("%s(%d): \n", __FUNCTION__, __LINE__);
}

static void uviot_impl_loop_poll_cb(uv_poll_t* handle, int status, int events) {
    struct uviot_impl* iot = NULL;
    struct mosquitto* mosq = NULL;

    if (!handle || !handle->data)
        return;

    printf("%s(%d): ....status:%d, events:%d\n", __FUNCTION__, __LINE__, status, events);
    iot = (struct uviot_impl*)handle->data;

    mosq = iot->mosq;

    if (!mosq)
        return;

    // mxp, 20250623, reconnect when socket is broken
    if (status == UV_EBADF) {
        HR_LOGE("EBADF poll %d status: %d, events:0x%X\n", iot->sock, status, events);
        // no trigger on_disconnect
        if (!uv_is_closing((uv_handle_t*)handle)) {
            uv_poll_stop(handle);
            uv_close((uv_handle_t*)handle, uviot__close_uv_dynamic_handle);
        }
        if (iot->auto_reconnect) {
            // stop & start reconnect timer callback
            uv_timer_start(&iot->timer, uviot_impl_connect_timer_cb, UVIOT_RECONNECT_DELAY, 0);
        }

        return;
    }

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

    // verify socket has been closed by us
    if (mosquitto_socket(mosq) == -1) {
        HR_LOGD("socket is invalid, we should stop poll\n");
        if (!uv_is_closing((uv_handle_t*)handle)) {
            uv_poll_stop(handle);
            uv_close((uv_handle_t*)handle, uviot__close_uv_dynamic_handle);
        }
        if (iot->auto_reconnect) {
            // stop & start reconnect timer callback
            uv_timer_start(&iot->timer, uviot_impl_connect_timer_cb, UVIOT_RECONNECT_DELAY, 0);
        }

        return;
    }

#if 1
    if (events & UV_DISCONNECT) {
        HR_LOGD("%s(%d): come in disconnect.......\n", __FUNCTION__, __LINE__);
        // stop current poll, we should reconnect and using new socket
        if (!uv_is_closing((uv_handle_t*)handle)) {
            uv_poll_stop(handle);
            uv_close((uv_handle_t*)handle, uviot__close_uv_dynamic_handle);
        }

        if (iot->auto_reconnect) {
            // stop & start reconnect timer callback
            uv_timer_start(&iot->timer, uviot_impl_connect_timer_cb, UVIOT_RECONNECT_DELAY, 0);
        }

        return;
    }
#endif

    if (pevents != iot->pevents) {
        iot->pevents = pevents;
        uv_poll_start(&iot->poll, pevents, uviot_impl_loop_poll_cb);
    }
}

static struct uviot__topic* iot__topic_new(struct uviot_impl* iot, const struct uviot_topic* topic) {
    struct uviot__topic* t = NULL;
    if (!topic)
        return NULL;

    t = (struct uviot__topic*)calloc(1, sizeof(struct uviot__topic));
    if (!t) {
        return NULL;
    }

    t->iot = iot;
    t->self = topic;

    // init timer later, because now maybe no loop
    if (t->self->type == TOPIC_TYPE_PUBLISH && t->self->period > 0) {
        t->timer = (uv_timer_t*)calloc(1, sizeof(uv_timer_t));
        uv_timer_init(iot->loop, t->timer);
        t->timer->data = t;
        t->refs++;
    }
    if (t->self->type == TOPIC_TYPE_PUBLISH /*&& !p->async*/) {
        t->async = (uv_async_t*)calloc(1, sizeof(uv_async_t));
        t->async->data = t;
        uv_async_init(iot->loop, t->async, iot__topic_async_cb);
        t->refs++;
    }

    HR_INIT_LIST_HEAD(&t->entry);

    // attach topic to iot
    hr_list_add_tail(&t->entry, &iot->topic_head);
    return t;
}

static void iot__topic_close_uv_dynamic_handle(uv_handle_t* handle) {
    struct uviot__topic* t = (struct uviot__topic*)handle->data;
    HR_LOGD("%s(%d): close :%p -> %s (%d)\n", __FUNCTION__, __LINE__, handle, t->self->name, t->refs);
    free(handle);
    t->refs--;
    if (t->refs == 0) {
        HR_LOGD("%s(%d): free topic close :%p\n", __FUNCTION__, __LINE__, t);
        memset((void*)t, 0, sizeof(struct uviot__topic));
        free(t);
    }
}
static void iot__topic_free(struct uviot__topic* t) {
    if (!t) {
        return;
    }

    // no lock ...
    hr_list_del(&t->entry);

    HR_INIT_LIST_HEAD(&t->entry);

    // release stash pending message
    if (t->stash.data) {
        while (t->stash.tail < t->stash.head) {
            int id = t->stash.tail & (t->stash.caps - 1);
            if (t->stash.data[id]) {
                free((void*)t->stash.data[id]);
            }
            t->stash.tail++;
        }

        free(t->stash.data);
        t->stash.data = NULL;
    }

    HR_LOGD("%s(%d): free :%p -> %s (%d)\n", __FUNCTION__, __LINE__, t, t->self->name, t->refs);
    if (t->timer != NULL) {
        uv_close((uv_handle_t*)t->timer, iot__topic_close_uv_dynamic_handle);
        t->timer = NULL;
    }
    if (t->async) {
        uv_close((uv_handle_t*)t->async, iot__topic_close_uv_dynamic_handle);
        t->async = NULL;
    }
    // should not directl free, we should wait callback
    // see iot__topic_close_uv_dynamic_handle
    // memset((void*)t, 0, sizeof(struct uviot__topic));
    // free(t);
    if (t->refs == 0) {
        free(t);
    }
}

struct uviot* uviot_alloc(uv_loop_t* loop) {
    struct uviot_impl* iot = (struct uviot_impl*)calloc(1, sizeof(struct uviot_impl));
    if (!iot) {
        return NULL;
    }

    if (mosquitto_lib_refs == 0) {
        mosquitto_lib_refs++;
        mosquitto_lib_init();
    }

    iot->loop = loop;

    HR_INIT_LIST_HEAD(&iot->topic_head);

    iot->self.alive_time = 60;

    // defined in ssl.h
    // # define SSL_VERIFY_NONE                 0x00
    // # define SSL_VERIFY_PEER                 0x01
    iot->self.tls_cert_reqs = 1;
    iot->self.tls_insecre = 0;

    uv_timer_init(loop, &iot->timer);
    iot->timer.data = iot;

    iot->refs = 1;

    return &iot->self;
}

static void uviot__close_uv_dynamic_handle(uv_handle_t* handle) {
    HR_LOGD("%s(%d): close :%p\n", __FUNCTION__, __LINE__, handle);
    struct uviot_impl* iot = (struct uviot_impl*)handle->data;
    if (!iot) {
        return;
    }

    iot->refs--;
    HR_LOGD("%s(%d): close :%p refs:%d\n", __FUNCTION__, __LINE__, handle, iot->refs);
    if (iot->refs == 0) {
        memset((void*)iot, 0, sizeof(*iot));
        free(iot);
    }
}
int uviot_release(struct uviot* self) {
    uv_loop_t* loop = NULL;
    struct uviot__topic *n = NULL, *p = NULL;
    struct uviot_impl* iot = container_of(self, struct uviot_impl, self);
    if (!self || !iot) {
        return -1;
    }

    HR_LOGE("%s(%d): iot:%p uviot_impl:%p, refs:%d\n", __FUNCTION__, __LINE__, &iot->self, iot, iot->refs);

    // do not auto connect anymore!
    iot->auto_reconnect = 0;

    mosquitto_disconnect(iot->mosq);

    loop = iot->poll.loop;

    HR_LOGE("%s(%d): iot:%p uviot_impl:%p, refs:%d\n", __FUNCTION__, __LINE__, &iot->self, iot, iot->refs);
    uv_cancel((uv_req_t*)&iot->conn_req);
    HR_LOGE("%s(%d): iot:%p uviot_impl:%p, refs:%d\n", __FUNCTION__, __LINE__, &iot->self, iot, iot->refs);
    // we must verify, because iot->poll maybe close in running
    if (uv_has_ref((uv_handle_t*)&iot->poll) /*iot->poll.type != UV_UNKNOWN_HANDLE*/) {
        if (!uv_is_closing((const uv_handle_t*)&iot->poll)) {
            uv_poll_stop(&iot->poll);
        }
    }

    uv_timer_stop(&iot->timer);

    hr_list_for_each_entry_safe(p, n, &iot->topic_head, entry) {
        iot__topic_free(p);
    }

    HR_INIT_LIST_HEAD(&iot->topic_head);

    // fixed valgrind memory problem
    // free memory after loop
    // uv_run(loop, UV_RUN_ONCE /*UV_RUN_DEFAULT*/);
    // must free after loop finished
    // prepare shutdown
    // iot->refs = 2;
    iot->poll.data = iot;
    iot->timer.data = iot;

    // when network is broken, poll maybe invalid
    if (uv_has_ref((uv_handle_t*)&iot->poll) /*iot->poll.type != UV_UNKNOWN_HANDLE*/) {
        if (!uv_is_closing((const uv_handle_t*)&iot->poll)) {
            uv_close((uv_handle_t*)&iot->poll, uviot__close_uv_dynamic_handle);
        }
    }
    HR_LOGE("%s(%d): iot:%p uviot_impl:%p\n", __FUNCTION__, __LINE__, &iot->self, iot);
    uv_close((uv_handle_t*)&iot->timer, uviot__close_uv_dynamic_handle);

    mosquitto_destroy(iot->mosq);

    // do not call free directly
    // it will auto release in uviot__close_uv_dynamic_handle
    // free(iot);

    mosquitto_lib_refs--;

    if (mosquitto_lib_refs == 0) {
        mosquitto_lib_cleanup();
    }

    return 0;
}

int uviot_prepare(struct uviot* self) {
    int rc = -1;
    // struct uviot__topic* p = NULL;
    struct mosquitto* mosq = NULL;
    struct uviot_impl* iot = container_of(self, struct uviot_impl, self);

    HR_LOGE("%s(%d): id:%s\n", __FUNCTION__, __LINE__, self->id);
    if (!self || !iot) {
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

    if (self->ca_file || self->ca_path || self->certificate || self->certificate_key) {
        mosquitto_tls_set(mosq, self->ca_file, self->ca_path, self->certificate, self->certificate_key, NULL);
        mosquitto_tls_insecure_set(mosq, self->tls_insecre);
        mosquitto_tls_opts_set(mosq, self->tls_cert_reqs, NULL, NULL);
    }

    mosquitto_username_pw_set(iot->mosq, self->username, self->password);

    // run immediately after loop started
    uv_timer_start(&iot->timer, uviot_impl_connect_timer_cb, 0, 0);
    return 0;
}

int uviot_topic_register(struct uviot* self, const struct uviot_topic* topic) {
    struct uviot_impl* iot = container_of(self, struct uviot_impl, self);
    struct uviot__topic* t = NULL;
    if (!self || !iot || !topic) {
        return -1;
    }

    t = iot__topic_new(iot, topic);
    if (!t) {
        return -1;
    }

    return 0;
}

int uviot_publish_async(struct uviot* self, const struct uviot_topic* topic) {
    struct uviot_impl* iot = container_of(self, struct uviot_impl, self);

    if (!self || !iot || !topic) {
        return -1;
    }

    if (topic->type != TOPIC_TYPE_PUBLISH) {
        HR_LOGE("%s(%d): topic is not publish: %s\n", __FUNCTION__, __LINE__, topic->name);
        return -1;
    }
    struct uviot__topic* p = NULL;
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

const char* uviot_get_connection_ipv4_address(struct uviot* self) {
    struct uviot_impl* iot = container_of(self, struct uviot_impl, self);
    if (!self || !iot) {
        return "";
    }
    return iot->status.ipv4;
}
const char* uviot_get_connection_mac_address(struct uviot* self) {
    struct uviot_impl* iot = container_of(self, struct uviot_impl, self);
    if (!self || !iot) {
        return "";
    }

    return iot->status.mac;
}
