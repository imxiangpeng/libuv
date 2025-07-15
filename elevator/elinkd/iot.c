
// mxp, 20250709, implement iot model, running in moquitto inner thread
// do not call ubus function in this thread

#define _GNU_SOURCE
#include "iot.h"

#include <assert.h>
#include <mosquitto.h>
#include <mqtt_protocol.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <pthread.h>
#include <resolv.h>
#include <string.h>

#include "hr_log.h"
#include "libubox/list.h"
#include "libubox/uloop.h"
#include "topic.h"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

#if 1 // aliyun
#define BROKER_DEFAULT_SERVER "a1z1g0btxvW.iot-as-mqtt.cn-shanghai.aliyuncs.com"
#define BROKER_DEFAULT_PORT 1883     // 1883     // 8883 //1883

// https://living.aliyun.com/project/a123Vlj9ublcLvZq/dev/
#define TIHUIYAN_PRODUCT_KEY "a1z1g0btxvW"
#define TIHUIYAN_DEVICE_SECRET "130cdc9746be2eeaad3ca8caaf989137"
#define TIHUIYAN_DEVICE_NAME "LC123456789"
#else
// inspur iot
#define BROKER_DEFAULT_SERVER "8.140.58.68"
#define BROKER_DEFAULT_PORT 51883
#define TIHUIYAN_PRODUCT_KEY "G9k7UzhGu8AZtlOf"
#define TIHUIYAN_DEVICE_SECRET "bab303e54f94bc8b722177365f8522f6"
#define TIHUIYAN_DEVICE_NAME "5CB4E213B8CB"
#endif

#define BROKER_DEFAULT_ALIVETIME 60  // 300 //60                       // 60s
#define BROKER_DEFAULT_TLS_INSECURE true
#define BROKER_DEFAULT_TLS_CERT_REQS 0
#define BROKER_DEFAULT_CA_FILE NULL
#define BROKER_DEFAULT_CA_PATH NULL
#define BROKER_DEFAULT_CERTIFICATE NULL
#define BROKER_DEFAULT_CERTIFICATE_KEY NULL

struct iot__topic {
    int mid;
    const struct topic* self;
    struct list_head entry;
    // period topic, auto publish

    struct uloop_timeout timer;
};

struct iot_priv {
    struct mosquitto* mosq;

    struct list_head topic_head;
};

static struct iot_priv _priv = {
    .mosq = NULL,
    .topic_head = LIST_HEAD_INIT(_priv.topic_head),
};

void post_timer(struct uloop_timeout* t, int msec);

static void iot__topic_timeout_task_cb(struct uloop_timeout* t) {
    if (!t) return;
    struct iot__topic* topic = container_of(t, struct iot__topic, timer);

    if (topic->self->period > 0) {
        // it's in uloop, also you can use uloop_timeout_set directly
        post_timer /*uloop_timeout_set*/ (&topic->timer, topic->self->period);
    }
    if (mosquitto_socket(_priv.mosq) == -1) {
        return;
    }

    // public topics
    void* payload = NULL;
    int len = 0;
    topic->self->callback.on_publish(&payload, &len);
    if (payload != NULL && len > 0) {
        int rc = mosquitto_publish(_priv.mosq, &topic->mid, topic->self->topic,
                                   len, (const void*)payload,
                                   0, false);
        if (rc != MOSQ_ERR_SUCCESS) {
            HR_LOGE("publish failed :%d\n", rc);
        }
        free(payload);
    }
}

static struct iot__topic* iot__topic_new(const struct topic* topic) {
    struct iot__topic* t = NULL;
    if (!topic)
        return NULL;

    t = (struct iot__topic*)calloc(1, sizeof(struct iot__topic));
    if (!t) {
        return NULL;
    }

    t->self = topic;

    // init timer later, because now maybe no loop
    if (t->self->type == TOPIC_TYPE_PUBLISH /*&& t->self->period > 0*/) {
        t->timer.cb = iot__topic_timeout_task_cb;
    }

    INIT_LIST_HEAD(&t->entry);

    // attach topic to iot
    list_add_tail(&t->entry, &_priv.topic_head);
    return t;
}

static void iot__topic_free(struct iot__topic* t) {
    if (!t) {
        return;
    }

    // no lock ...
    list_del(&t->entry);

    INIT_LIST_HEAD(&t->entry);

    HR_LOGD("%s(%d): free :%p -> %s\n", __FUNCTION__, __LINE__, t, t->self->name);
    if (!t->timer.cb) {
        uloop_timeout_cancel(&t->timer);
        t->timer.cb = NULL;
    }

    free(t);
}

static void _on_connect(struct mosquitto* mosq, void* obj, int reason) {
    (void)obj;
    HR_LOGD("%s(%d): reason :%d\n", __FUNCTION__, __LINE__, reason);
    struct iot_priv* priv = (struct iot_priv*)obj;
    if (!mosq || !priv)
        return;

    if (CONNACK_ACCEPTED != reason) {
        HR_LOGD("Connection error: %s\n", mosquitto_connack_string(reason));
        return;
    }
    HR_LOGD("%s(%d): connected, ...\n", __FUNCTION__, __LINE__);

    struct iot__topic* p = NULL;
    list_for_each_entry(p, &priv->topic_head, entry) {
        if (p->self->type == TOPIC_TYPE_SUBSCRIBE) {
            int ret = mosquitto_subscribe(mosq, &p->mid, p->self->topic, 0);
            HR_LOGD("%s(%d): connected, auto subscribe:%s -> (%d)\n", __FUNCTION__, __LINE__, p->self->topic, ret);
        } else {
            // public topics
            void* payload = NULL;
            int len = 0;
            if (p->self->auto_publish != 0) {
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

            if (p->self->period > 0) {
                HR_LOGD("%s(%d): topic %s...start timer :%d, p->timer.cb:%p vs %p\n", __FUNCTION__, __LINE__, p->self->topic, p->self->period, p->timer.cb, iot__topic_timeout_task_cb);
                // it's not uloop, do not use uloop_timeout_set directly
                post_timer(&p->timer, p->self->period);
            }
        }
    }
}

static void _on_disconnect(struct mosquitto* mosq, void* userdata, int rc) {
    (void)mosq;
    (void)userdata;
    (void)rc;
    HR_LOGD("%s(%d): \n", __FUNCTION__, __LINE__);
}

static void _on_message(struct mosquitto* mosq, void* obj, const struct mosquitto_message* message) {
    (void)mosq;
    struct iot__topic* p = NULL;
    struct iot_priv* priv = (struct iot_priv*)obj;
    if (!mosq || !priv)
        return;
#if 0
    HR_LOGD("%s(%d): receive topic:%s, payloadlen:%d\n", __FUNCTION__, __LINE__,
            message->topic, message->payloadlen);
    if (message->payload) {
        HR_LOGD("%s(%d): receive topic:%s, payloadlen:%d\n%s\n", __FUNCTION__,
                __LINE__, message->topic, message->payloadlen, (const char*)message->payload);
    }
#endif

    if (!message->payload) {
        return;
    }

    list_for_each_entry(p, &_priv.topic_head, entry) {
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

        // allow multi place subscribe same topic?
        break;
    }
}

int iot_init() {
    int rc = -1;
    struct mosquitto* mosq = NULL;
    char name[64] = {TIHUIYAN_DEVICE_NAME};
    char id[256] = {0};
    char username[256] = {0};
    char password[256] = {0};

    char hmac_secret[128] = {TIHUIYAN_DEVICE_SECRET};

    char* iot_content = NULL;
    unsigned char result[EVP_MAX_MD_SIZE] = {0};
    unsigned int len = EVP_MAX_MD_SIZE;
    char* product_key = TIHUIYAN_PRODUCT_KEY;

    // char* client_id = TIHUIYAN_PRODUCT_KEY "." TIHUIYAN_DEVICE_NAME;
    char client_id[128] = {0};  // TIHUIYAN_PRODUCT_KEY "." TIHUIYAN_DEVICE_NAME;

    long long msec = 0;
    struct timespec ts;

    if (clock_gettime(CLOCK_REALTIME, &ts) == -1) {
        return -1;
    }
    msec = (ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL);

    // get mac address into name buffer
    // platform_get_property(PROPERTY_MACADDR, name, sizeof(name));
    // printf("mac:%s\n", name);
    size_t i = 0, j = 0;
    for (; name[i] != '\0'; i++) {
        if (name[i] != ':') {
            name[j] = name[i];
            j++;
        }
    }
    name[j] = '\0';

    printf("device name:%s\n", name);

    // platform_get_property(PROPERTY_DEVICE_SECRET, hmac_secret, sizeof(hmac_secret));
    // printf("device secret:%s\n", hmac_secret);

    // client can be mac or any other custom string
    // we use format: TIHUIYAN_PRODUCT_KEY.TIHUIYAN_DEVICE_NAME
    snprintf(client_id, sizeof(client_id), "%s.%s", TIHUIYAN_PRODUCT_KEY, name);
    printf("clientid:%s\n", client_id);

    // mqttClientId: clientId+"|securemode=3,signmethod=hmacsha1,timestamp=132323232|"
    // mqttUsername: deviceName+"&"+productKey
    // mqttPassword: sign_hmac(deviceSecret,content)
    snprintf(id, sizeof(id), "%s|securemode=2,signmethod=hmacsha256,timestamp=%lld|", client_id, msec);
    // {deviceName}&{productKey}
    snprintf(username, sizeof(username), "%s&%s", name, product_key);
    // "clientId" + {ClientId}+ "deviceName" + {deviceName }+ "productKey" + {productKey }+ "timestamp" + {timestamp}
    asprintf(&iot_content, "clientId%sdeviceName%sproductKey%stimestamp%lld", client_id, name, product_key, msec);

    printf("username:%s\n", username);
    printf("%s(%d): ....hmac_secret:%s\nn", __FUNCTION__, __LINE__, hmac_secret);
    HMAC(EVP_sha256(), hmac_secret, strlen(hmac_secret), (unsigned char*)iot_content, strlen(iot_content), result, &len);

    assert(sizeof(password) >= EVP_MAX_MD_SIZE * 2 + 1);

    for (unsigned int i = 0; i < len; i++) {
        sprintf(password + i * 2, "%02x", result[i]);
    }
    password[len * 2] = '\0';
    printf("iot password:%s\n", password);

    // free memory
    free(iot_content);
    iot_content = NULL;

    // https://help.aliyun.com/zh/iot/user-guide/establish-mqtt-connections-over-tcp
    // _iot->ca_file = "~/Downloads/ali_iot_ca.crt";

    mosquitto_lib_init();

    topic_init(product_key, name);

    mosq = mosquitto_new(id, true, &_priv);
    if (!mosq) {
        mosquitto_lib_cleanup();
        return -1;
    }

    _priv.mosq = mosq;

    mosquitto_connect_callback_set(mosq, _on_connect);
    // mosquitto_connect_with_flags_callback_set(mosq,
    // _on_connect_with_flags);
    mosquitto_disconnect_callback_set(mosq, _on_disconnect);
    mosquitto_message_callback_set(mosq, _on_message);

    mosquitto_tls_set(mosq, BROKER_DEFAULT_CA_FILE, BROKER_DEFAULT_CA_PATH, BROKER_DEFAULT_CERTIFICATE, BROKER_DEFAULT_CERTIFICATE_KEY, NULL);
    mosquitto_tls_insecure_set(mosq, BROKER_DEFAULT_TLS_INSECURE);
    mosquitto_tls_opts_set(mosq, BROKER_DEFAULT_TLS_CERT_REQS, NULL, NULL);

    mosquitto_username_pw_set(mosq, username, password);

    // no error async interface
    mosquitto_connect_async(_priv.mosq, BROKER_DEFAULT_SERVER, BROKER_DEFAULT_PORT, BROKER_DEFAULT_ALIVETIME);
    if (rc != MOSQ_ERR_SUCCESS) {
        // async maybe return MOSQ_ERR_CONN_PENDING
        HR_LOGD("%s(%d): connect result:%d -> %s\n", __FUNCTION__, __LINE__, rc, mosquitto_strerror(rc));
        if (rc == MOSQ_ERR_EAI) {
            res_init();
        }
    }

    // mxp, 20250703, use inner loop thread, you should call it after connected
    // but i found it will loop wait connect state
    mosquitto_loop_start(_priv.mosq);

    return 0;
}

int iot_deinit() {
    struct mosquitto* mosq = _priv.mosq;

    struct iot__topic *n = NULL, *p = NULL;
    if (!mosq) {
        return -1;
    }

    mosquitto_disconnect(mosq);
    mosquitto_loop_stop(mosq, false);
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();

    list_for_each_entry_safe(p, n, &_priv.topic_head, entry) {
        iot__topic_free(p);
    }

    INIT_LIST_HEAD(&_priv.topic_head);
    return 0;
}

int iot_topic_register(const struct topic* topic) {
    (void)topic;
    struct iot__topic* t = NULL;
    if (!topic) {
        return -1;
    }

    t = iot__topic_new(topic);
    if (!t) {
        return -1;
    }

    return 0;
}

int iot_topic_publish_async(const struct topic* topic) {
    (void)topic;
    struct iot__topic* p = NULL;

    if (!topic) return -1;

    if (topic->type != TOPIC_TYPE_PUBLISH) {
        HR_LOGE("%s(%d): topic is not publish: %s\n", __FUNCTION__, __LINE__, topic->name);
        return -1;
    }
    list_for_each_entry(p, &_priv.topic_head, entry) {
        // ignore publish response message
        if (p->self->type != TOPIC_TYPE_PUBLISH) {
            continue;
        }

        if (p->self == topic) {
            // it's not uloop, do not use uloop_timeout_set directly
            post_timer(&p->timer, 0);
        }
    }

    return 0;
}
