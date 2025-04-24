// mxp, 20250310 sample skeleton mqtt

#define _GNU_SOURCE
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
// #include <json-c/json.h>
// #include <json-c/json_object.h>
#include <cjson/cJSON.h>
#include <ifaddrs.h>
#include <limits.h>
#include <math.h>
#include <mosquitto.h>
#include <mqtt_protocol.h>
#include <net/if.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <uv.h>

#include "board_platform.h"
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

#define DM_DEFAULT_PRODUCT_KEY "XXXX-XXXX"
#define DM_DEFAULT_PRODUCT_SECRET "XXXX-XXXX"

#define BROKER_DEFAULT_SERVER "a1z1g0btxvW.iot-as-mqtt.cn-shanghai.aliyuncs.com"
#define BROKER_DEFAULT_PORT 1883     // 8883 //1883
#define BROKER_DEFAULT_ALIVETIME 60  // 300 //60                       // 60s
// https://living.aliyun.com/project/a123Vlj9ublcLvZq/dev/
#define TIHUIYAN_PRODUCT_KEY "a1z1g0btxvW"
#define TIHUIYAN_DEVICE_SECRET "130cdc9746be2eeaad3ca8caaf989137"
#define TIHUIYAN_DEVICE_NAME "LC123456789"

#define DEFAULT_POLL_EVENTS (UV_READABLE | UV_DISCONNECT) /*| UV_WRITABLE*/
struct iot_mosquitto;

struct iot_config {
    struct {
        char* server;
        int port;
    } broker;
    struct {
        char* product_key;
        char* name;
        char* secret;
    } device;
};

struct dm__topic {
    struct iot_mosquitto* iot;
    int mid;
    const struct dm_topic* self;
    struct hr_list_head entry;
    // period topic, auto publish
    uv_timer_t* timer;
};

struct iot_mosquitto {
    bool is_dynamic_register;
    bool auto_reconnect;
    int sock;  // mosquitto socket
    struct mosquitto* mosq;

    uv_timer_t timer;
    uv_poll_t poll;
    int pevents;

    // struct hr_list_head topic_head;
};

static HR_LIST_HEAD(_topic_list);

struct dm_platform {
    struct iot_config conf;
    char host[256];
    int port;
    int alive_time;  // 60s
    char username[256];
    char password[256];
    char client_id[128];
    int qos;

    // board system information
    char serialno[256];
    char mac[18];  // board burn mac address
    char STBID[256];

    // struct mosquitto *mosq;
    struct iot_mosquitto* iot;
    // int sock;  // mosquitto socket
    int mosq_have_connected;
    uv_loop_t* loop;
    // uv_poll_t mosq_poll;
    // int mosq_pevents;
    // uv_timer_t timer;

    struct {
        char ipv4[INET_ADDRSTRLEN];
        char mac[18];
    } status;
};
static struct dm_platform _plat = {0};

static struct iot_mosquitto* mosquitto_iot_new(const char* id);
static int mosquitto_iot_reinitialize(struct iot_mosquitto* iot, const char* id);
static int mosquitto_iot_free(struct iot_mosquitto* iot);

static int _load_config(struct iot_config* conf);
static int _store_config(struct iot_config* conf);

static void _mosquitto_reconnect_timer_cb(uv_timer_t* handle);
static void _topic_period_timer_cb(uv_timer_t* handle) {
    if (!handle || !handle->data)
        return;
    struct dm__topic* t = (struct dm__topic*)handle->data;

    HR_LOGD("%s(%d): publish topic: %s ...\n", __FUNCTION__, __LINE__, t->self->name);

    // public topics
    void* payload = NULL;
    int len = 0;
    t->self->callback.on_publish(&payload, &len);
    if (payload != NULL && len > 0) {
        int ret = mosquitto_publish(_plat.iot->mosq, &t->mid, t->self->topic,
                                    len, (const void*)payload,
                                    0, false);
        free(payload);
    }
}

static void dm__topic_timer_start(struct dm__topic* t) {
    if (!t || !t->self)
        return;
    if (t->timer != NULL) {
        printf("%s(%d): period:%d\n", __FUNCTION__, __LINE__, t->self->period);
        uv_timer_start(t->timer, _topic_period_timer_cb, t->self->period, t->self->period);
    }
}
static void dm__topic_timer_stop(struct dm__topic* t) {
    if (!t || !t->self)
        return;
    if (t->timer != NULL) {
        uv_timer_stop(t->timer);
    }
}
static struct dm__topic* dm__topic_new(const struct dm_topic* topic) {
    struct dm__topic* t = NULL;
    if (!topic)
        return NULL;

    t = (struct dm__topic*)calloc(1, sizeof(struct dm__topic));
    if (!t) {
        return NULL;
    }

    t->self = topic;

    if (t->self->type == TOPIC_TYPE_PUBLISH && t->self->period > 0) {
        t->timer = (uv_timer_t*)calloc(1, sizeof(uv_timer_t));
        uv_timer_init(_plat.loop, t->timer);
        t->timer->data = t;
    }
    HR_INIT_LIST_HEAD(&t->entry);

    hr_list_add_tail(&t->entry, &_topic_list);

    return t;
}

void _close_uv_dynamic_handle(uv_handle_t* handle) {
    HR_LOGD("%s(%d): close :%p\n", __FUNCTION__, __LINE__, handle);
    free(handle);
}

static void dm__topic_free(struct dm__topic* t) {
    if (!t) {
        return;
    }

    // no lock ...
    hr_list_del(&t->entry);

    HR_INIT_LIST_HEAD(&t->entry);

    if (t->timer != NULL) {
        HR_LOGD("%s(%d): close :%p\n", __FUNCTION__, __LINE__, t->timer);
        uv_close((uv_handle_t*)t->timer, _close_uv_dynamic_handle);
    }
    memset((void*)t, 0, sizeof(struct dm__topic));
    free(t);
}

// you must free the pointer
static size_t _read_file(const char* path, char** buf) {
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

    data = (char*)malloc(sb.st_size);
    if (!data) {
        close(fd);
        return -1;
    }
    memset((void*)data, 0, sb.st_size);

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

static ssize_t _write_file_fd(int fd, char* data, size_t size) {
    ssize_t left = size;
    char* ptr = data;

    if (fd < 0 || !data || size == 0)
        return -1;

    while (left > 0) {
        ssize_t n = TEMP_FAILURE_RETRY(write(fd, ptr, left));
        if (n == -1) {
            return size - left;
        }
        ptr += n;
        left -= n;
    }

    return size;
}

static ssize_t _write_file(const char* path, char* data, size_t size) {
    int fd = -1;

    if (!data || !path || size <= 0) {
        return -1;
    }

    fd = open(path, O_RDWR | O_TRUNC | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
    if (fd < 0) {
        return -1;
    }

    size = _write_file_fd(fd, data, size);
    fdatasync(fd);
    close(fd);

    return size;
}

static void _update_connection_status(int sock) {
    struct ifreq ifr;
    struct sockaddr_in addr;
    socklen_t addr_len = sizeof(addr);

    if (sock < 0)
        return;

    if (0 != getsockname(sock, (struct sockaddr*)&addr, &addr_len)) {
        return;
    }

    inet_ntop(AF_INET, &addr.sin_addr, _plat.status.ipv4, sizeof(_plat.status.ipv4));
    uint16_t port = ntohs(addr.sin_port);

    HR_LOGD("Local IP: %s, Port: %d\n", _plat.status.ipv4, port);

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

    snprintf(_plat.status.mac, sizeof(_plat.status.mac), "%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    HR_LOGD("now %s -> %s\n", _plat.status.mac, _plat.status.ipv4);
}

static void _on_log(struct mosquitto* mosq, void* obj, int level, const char* str) {
    (void)mosq;
    (void)obj;
    (void)level;
    HR_LOGD("MQTT %s\n", str);
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
        struct dm__topic *p = NULL, *msg = NULL;
        hr_list_for_each_entry(p, &_topic_list, entry) {
            if (p->self->type == TOPIC_TYPE_SUBSCRIBE) {
                int ret = mosquitto_subscribe(mosq, &p->mid, p->self->topic, 0);
                HR_LOGD("%s(%d): connected, auto subscribe:%s -> (%d)\n", __FUNCTION__, __LINE__, p->self->topic, ret);
            } else {
                // public topics
                void* payload = NULL;
                int len = 0;
                p->self->callback.on_publish(&payload, &len);
                if (payload != NULL && len > 0) {
                    int ret = mosquitto_publish(mosq, &p->mid, p->self->topic,
                                                len, (const void*)payload,
                                                0, false);
                    HR_LOGD("%s(%d): connected, auto publish:%s -> (%d)\n", __FUNCTION__, __LINE__, p->self->topic, ret);
                    free(payload);
                }

                if (p->timer != NULL) {
                    dm__topic_timer_start(p);
                }
            }
        }
        _update_connection_status(iot->sock);

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

// {
//   "productKey": "0451ba96-6504-4281-9417-4094d932cf34",
//   "deviceName": "INSJ24999901",
//   "deviceSecret": "2d216a1abae442ae9207ffaa1b7db2fa"
// }
static int _parse_register_response(const char* data, unsigned int length) {
    const char* product_key = NULL;
    const char* device_name = NULL;
    const char* device_secret = NULL;

    cJSON* root = cJSON_ParseWithLength(data, length);
    if (!root)
        return -1;

    product_key = cJSON_GetStringValue(cJSON_GetObjectItem(root, "productKey"));
    device_name = cJSON_GetStringValue(cJSON_GetObjectItem(root, "deviceName"));
    device_secret = cJSON_GetStringValue(cJSON_GetObjectItem(root, "deviceSecret"));

    if (!product_key || !device_name || !device_secret) {
        cJSON_Delete(root);
        return -1;
    }

    if (_plat.conf.device.product_key) {
        free(_plat.conf.device.product_key);
        _plat.conf.device.product_key = strdup(product_key);
    }

    if (_plat.conf.device.name) {
        free(_plat.conf.device.name);
        _plat.conf.device.name = strdup(device_name);
    }

    if (_plat.conf.device.secret) {
        free(_plat.conf.device.secret);
        _plat.conf.device.secret = strdup(device_secret);
    }

    cJSON_Delete(root);

    _store_config(&_plat.conf);
    return 0;
}
static void _on_message(struct mosquitto* mosq, void* obj, const struct mosquitto_message* message) {
    (void)mosq;
    const char* method = NULL;
    uint32_t id = 1;

    struct iot_mosquitto* iot = (struct iot_mosquitto*)obj;
    if (!mosq || !iot)
        return;
    HR_LOGD("%s(%d): receive topic:%s, payloadlen:%d\n", __FUNCTION__, __LINE__,
            message->topic, message->payloadlen);
    if (message->payload) {
        HR_LOGD("%s(%d): receive topic:%s, payloadlen:%d\n%s\n", __FUNCTION__,
                __LINE__, message->topic, message->payloadlen, message->payload);
    }

    // HR_LOGD("is_dynamic_register:%d\n", iot->is_dynamic_register);

    if (!message->payload)
        return;

    struct dm__topic* p = NULL;
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

static void _on_publish(struct mosquitto* mosq, void* userdata, int mid) {
    HR_LOGD("%s(%d): \n", __FUNCTION__, __LINE__);
}

static void _signal_action(int signum, siginfo_t* siginfo, void* sigcontext) {
    (void)sigcontext;

    HR_LOGD("%s(%d): ........signum:%d\n", __FUNCTION__, __LINE__, signum);

    if (SIGUSR1 == signum) {
        mosquitto_disconnect(_plat.iot->mosq);
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
static int _load_config(struct iot_config* conf) {
    size_t len = 0;
    char* data = NULL;
    cJSON* root = NULL;

    const char* path = DM_DEFAULT_CONFIG_DIR "/" DM_DEFAULT_CONFIG_NAME;
    if (!conf)
        return -1;

    len = _read_file(path, &data);
    if (!data) {
        return -1;
    }

    // unlink(path);

    root = cJSON_ParseWithLength(data, len);
    free(data);
    data = NULL;

    if (!root)
        return -1;

    cJSON* broker = cJSON_GetObjectItem(root, "broker");
    if (broker) {
        const char* server = cJSON_GetStringValue(cJSON_GetObjectItem(root, "server"));
        double port = cJSON_GetNumberValue(cJSON_GetObjectItem(root, "port"));
        if (server) {
            conf->broker.server = strdup(server);
        } else {
            conf->broker.server = strdup(BROKER_DEFAULT_SERVER);
        }
        if (isnan((double)port)) {
            port = BROKER_DEFAULT_PORT;
        }
        conf->broker.port = port;
    }

    cJSON* device = cJSON_GetObjectItem(root, "device");
    if (device) {
        const char* pkey = cJSON_GetStringValue(cJSON_GetObjectItem(root, "product_key"));
        if (pkey) {
            conf->device.product_key = strdup(pkey);
        }

        const char* name = cJSON_GetStringValue(cJSON_GetObjectItem(root, "name"));
        if (name) {
            conf->device.name = strdup(name);
        }

        const char* secret = cJSON_GetStringValue(cJSON_GetObjectItem(root, "secret"));
        if (secret) {
            conf->device.secret = strdup(secret);
        }
    }

    cJSON_Delete(root);

    return 0;
}

static int _replace_config(const char* path, char* data, unsigned int size) {
    int fd = -1;
    char* tmp = NULL;
    int tmp_len = 0;

    const char* TMPFILE_TEMPLATE = "tmp_XXXXXX";

    tmp_len = strlen(path) + strlen(TMPFILE_TEMPLATE) + 1;  // + '\0'

    tmp = (char*)calloc(1, tmp_len);  // hardcode 8(.XXXXXX + \0)
    if (!tmp)
        return -1;

    snprintf(tmp, tmp_len, "%s%s", path, TMPFILE_TEMPLATE);
    fd = mkostemp(tmp, O_RDWR | O_TRUNC | O_CREAT);
    if (fd < 0) {
        free(tmp);
        return -1;
    }

    fchmod(fd, S_IRUSR | S_IWUSR | S_IRGRP);

    _write_file_fd(fd, data, size);

    close(fd);

    unlink(path);
    rename(tmp, path);

    free(tmp);
    return 0;
}

static int _store_config(struct iot_config* conf) {
    size_t len = 0;
    char* data = NULL;
    cJSON* root = NULL;

    const char* path = DM_DEFAULT_CONFIG_DIR "/" DM_DEFAULT_CONFIG_NAME;
    if (!conf)
        return -1;

    root = cJSON_CreateObject();
    if (!root)
        return -1;

    cJSON* broker = cJSON_AddObjectToObject(root, "broker");
    if (broker) {
        cJSON_AddStringToObject(broker, "server", conf->broker.server);
        cJSON_AddNumberToObject(broker, "port", conf->broker.port);
    }

    cJSON* device = cJSON_AddObjectToObject(root, "device");
    if (device) {
        cJSON_AddStringToObject(device, "product_key", conf->device.product_key);
        cJSON_AddStringToObject(device, "name", conf->device.name);
        cJSON_AddStringToObject(device, "secret", conf->device.secret);
    }

    data = cJSON_Print(root);
    if (data) {
        int fd = -1;
        char* tmp = NULL;
        int tmp_len = 0;

        const char* TMPFILE_TEMPLATE = "tmp_XXXXXX";

        tmp_len = strlen(path) + strlen(TMPFILE_TEMPLATE) + 1;  // + '\0'

        tmp = (char*)calloc(1, tmp_len);  // hardcode 8(.XXXXXX + \0)
        if (!tmp) {
            cJSON_Delete(root);
            return -1;
        }

        snprintf(tmp, tmp_len, "%s%s", path, TMPFILE_TEMPLATE);
        fd = mkostemp(tmp, O_RDWR | O_TRUNC | O_CREAT);
        if (fd < 0) {
            free(tmp);
            cJSON_Delete(root);
            return -1;
        }

        fchmod(fd, S_IRUSR | S_IWUSR | S_IRGRP);

        _write_file_fd(fd, data, strlen(data));

        close(fd);

        unlink(path);
        rename(tmp, path);

        free(tmp);

        free(data);
    }
    cJSON_Delete(root);
    return 0;
}

static void _mosquitto_loop_poll_cb(uv_poll_t* handle, int status, int events) {
    HR_LOGD("%s(%d): come in .....status:%d, event:0x%X..\n", __FUNCTION__, __LINE__, status, events);

    int new_events = DEFAULT_POLL_EVENTS;

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
            uv_timer_start(&iot->timer, _mosquitto_reconnect_timer_cb, 1000, 1000);
        }

        return;
    }
#endif

    if (pevents != iot->pevents) {
        iot->pevents = pevents;
        uv_poll_start(&iot->poll, pevents, _mosquitto_loop_poll_cb);
    }
}

static void _mosquitto_loop_misc_timer_cb(uv_timer_t* handle) {
    struct iot_mosquitto* iot = (struct iot_mosquitto*)handle->data;

    // check is connected?
    mosquitto_loop_misc(iot->mosq);
}

static void _mosquitto_reconnect_timer_cb(uv_timer_t* handle) {
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
        HR_LOGD("%s(%d): failed reconnect:%s:%d\n", __FUNCTION__, __LINE__, _plat.host, _plat.port);
        return;
    }

    iot->sock = mosquitto_socket(mosq);
    // assert(wrapper->sock != 0);

    // using uv_poll_init update socket
    // any memory leak ?
    uv_poll_init(_plat.loop, &iot->poll, iot->sock);
    iot->poll.data = iot;
    iot->pevents = DEFAULT_POLL_EVENTS;
    uv_poll_start(&iot->poll, iot->pevents, _mosquitto_loop_poll_cb);

    uv_timer_stop(handle);
    uv_timer_start(&iot->timer, _mosquitto_loop_misc_timer_cb, 1000, 1000);
}

/* Fully close a loop */
static void close_walk_cb(uv_handle_t* handle, void* arg) {
    if (!uv_is_closing(handle))
        uv_close(handle, NULL);
}

static void close_loop(uv_loop_t* loop) {
    uv_walk(loop, close_walk_cb, NULL);
    uv_run(loop, UV_RUN_DEFAULT);
}
#define MAKE_VALGRIND_HAPPY(loop)         \
    do {                                  \
        close_loop(loop);                 \
        assert(0 == uv_loop_close(loop)); \
        uv_library_shutdown();            \
    } while (0)

static long long time_ms() {
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) == -1) {
        perror("clock_gettime");
        return -1;
    }
    return (ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL);
}

static struct iot_mosquitto* mosquitto_iot_new(const char* id) {
    struct mosquitto* mosq = NULL;
    struct iot_mosquitto* iot = (struct iot_mosquitto*)calloc(1, sizeof(struct iot_mosquitto));
    if (!iot) {
        printf("%s(%d): ............\n", __FUNCTION__, __LINE__);
        return NULL;
    }

    // HR_INIT_LIST_HEAD(&wrapper->topic_head);

    iot->sock = -1;

    mosq = mosquitto_new(id, true, iot);
    if (!mosq) {
        free(iot);
        HR_LOGD("%s(%d): error can not instance mosquitto\n", __FUNCTION__,
                __LINE__);
        return NULL;
    }

    iot->mosq = mosq;

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

    return iot;
}
static int mosquitto_iot_reinitialize(struct iot_mosquitto* iot, const char* id) {
    struct mosquitto* mosq = NULL;

    if (!iot || !iot->mosq)
        return -1;

    // HR_INIT_LIST_HEAD(&wrapper->topic_head);

    mosq = iot->mosq;
    iot->sock = -1;

    int ret = mosquitto_reinitialise(mosq, id, false, iot);
    if (MOSQ_ERR_SUCCESS != ret) {
        free(iot);
        HR_LOGD("%s(%d): error can not instance mosquitto\n", __FUNCTION__,
                __LINE__);
        return -1;
    }

    mosquitto_log_callback_set(mosq, _on_log);

    mosquitto_subscribe_callback_set(mosq, _on_subscribe);
    mosquitto_connect_callback_set(mosq, _on_connect);
    // mosquitto_connect_with_flags_callback_set(mosq,
    // _on_connect_with_flags);
    mosquitto_disconnect_callback_set(mosq, _on_disconnect);
    mosquitto_message_callback_set(mosq, _on_message);
    mosquitto_publish_callback_set(mosq, _on_publish);
    mosquitto_tls_opts_set(mosq, 0 /*SSL_VERIFY_NONE*/, NULL, NULL);

    // const char *cafile = "/home/alex/workspace/workspace/libuv/mqtt_cacert.pem";
    // mosquitto_tls_set(_data.mosq, cafile, NULL, NULL, NULL, NULL);
    // mosquitto_tls_insecure_set(mosq, 0);
    // mosquitto_tls_opts_set(mosq, 0, NULL, NULL);

    return 0;
}

#if 0
static int mosquitto_wrapper_regiter(struct mosquitto_iot *wrapper, const struct dm_topic *topic) {
    if (!wrapper || !topic) {
        return -1;
    }
    struct dm__topic *t = NULL;

    t = (struct dm__topic *)calloc(1, sizeof(struct dm__topic));
    if (!t) {
        return -1;
    }

    t->iot = wrapper;
    t->self = topic;

    if (t->self->type == TOPIC_TYPE_PUBLISH && t->self->period > 0) {
        t->timer = (uv_timer_t *)calloc(1, sizeof(uv_timer_t));
        uv_timer_init(_plat.loop, t->timer);
        t->timer->data = t;
    }
    HR_INIT_LIST_HEAD(&t->entry);

    hr_list_add_tail(&t->entry, &wrapper->topic_head);

    return 0;
}
#endif

static int mosquitto_iot_free(struct iot_mosquitto* iot) {
    // struct dm__topic *n, *p;
    if (!iot || !iot->mosq) {
        return -1;
    }

    uv_poll_stop(&iot->poll);
    uv_timer_stop(&iot->timer);

    uv_close((uv_handle_t*)&iot->poll, NULL);
    uv_close((uv_handle_t*)&iot->timer, NULL);

    mosquitto_destroy(iot->mosq);

    // hr_list_for_each_entry_safe(p, n, &iot->topic_head, entry) {
    //     dm__topic_free(p);
    // }

    memset((void*)iot, 0, sizeof(*iot));
    free(iot);
    return 0;
}

// 因为费用问题禁止使用动态注册
// 重启后，检测到已经注册过，那么就是正常进行连接
int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    int rc = 0;
    int retries = 0;
    struct iot_mosquitto* iot = NULL;
    bool is_dynamic_register = 0;

    char* iot_client_id = NULL;
    char* iot_username = NULL;
    char* iot_content = NULL;
    char iot_password[EVP_MAX_MD_SIZE * 2 + 1] = {0};
    char* hmac_secret = NULL;

    unsigned char result[EVP_MAX_MD_SIZE] = {0};
    unsigned int len = EVP_MAX_MD_SIZE;

    struct sigaction action;
    memset(&action, 0, sizeof(action));
    sigemptyset(&action.sa_mask);
    action.sa_sigaction = _signal_action;
    sigaction(SIGUSR1, &action, NULL);

    memset((void*)&_plat, 0, sizeof(_plat));

    _plat.qos = 0;
    _plat.alive_time = BROKER_DEFAULT_ALIVETIME;
    _plat.port = BROKER_DEFAULT_PORT;

    // _load_config(&_plat.conf);
    _plat.conf.broker.server = strdup(BROKER_DEFAULT_SERVER);
    _plat.conf.broker.port = BROKER_DEFAULT_PORT;

    _plat.conf.device.product_key = strdup(TIHUIYAN_PRODUCT_KEY);
    _plat.conf.device.name = strdup(TIHUIYAN_DEVICE_NAME);
    _plat.conf.device.secret = strdup(TIHUIYAN_DEVICE_SECRET);

#if 0
    board_system_unifykey_read("usid", _plat.serialno, sizeof(_plat.serialno));
    board_system_unifykey_read("mac", _plat.mac, sizeof(_plat.mac));
    board_system_unifykey_read("deviceid", _plat.mac, sizeof(_plat.mac));
#endif

    snprintf(_plat.serialno, sizeof(_plat.serialno), "%s.%s", TIHUIYAN_PRODUCT_KEY, TIHUIYAN_DEVICE_NAME);

    _plat.loop = uv_default_loop();

    dm_topic_init(_plat.conf.device.product_key, _plat.conf.device.name);

    mosquitto_lib_init();

    long long ts = time_ms();
    printf("ts:%lld vs %ld\n", ts, 1745406478068);
    // ts = 1745406478068;
    // ts =    1745406478068;

#if 0
    if (!_plat.conf.device.product_key || !_plat.conf.device.name || !_plat.conf.device.secret) {
        char *client_id = _plat.serialno;
        char *name = _plat.serialno;

        is_dynamic_register = true;
        hmac_secret = DM_DEFAULT_PRODUCT_SECRET;
        // https://help.aliyun.com/zh/iot/user-guide/mqtt-based-dynamic-registration?spm=a2c4g.11186623.0.0.675a7673BTRD5q#task-1545804
        // {clientId}|authType=register,timestamp={timestamp}|
        asprintf(&iot_client_id, "%s|authType=register,timestamp=%lld|", client_id, ts);
        // {deviceName}&{productKey}
        asprintf(&iot_username, "%s&%s", name, DM_DEFAULT_PRODUCT_KEY);
        // "deviceName" + {deviceName }+ "productKey" + {productKey }+ "timestamp" + {timestamp}
        asprintf(&iot_content, "deviceName%sproductKey%stimestamp%lld", name, DM_DEFAULT_PRODUCT_KEY, ts);

    } else {
#endif
    char* name = _plat.conf.device.name;
    char* client_id = _plat.serialno;
    char* product_key = _plat.conf.device.product_key;
    hmac_secret = _plat.conf.device.secret;
    // mqttClientId: clientId+"|securemode=3,signmethod=hmacsha1,timestamp=132323232|"
    // mqttUsername: deviceName+"&"+productKey
    // mqttPassword: sign_hmac(deviceSecret,content)
    asprintf(&iot_client_id, "%s|securemode=2,signmethod=hmacsha256,timestamp=%lld|", client_id, ts);
    // {deviceName}&{productKey}
    asprintf(&iot_username, "%s&%s", name, product_key);
    // "clientId" + {ClientId}+ "deviceName" + {deviceName }+ "productKey" + {productKey }+ "timestamp" + {timestamp}
    asprintf(&iot_content, "clientId%sdeviceName%sproductKey%stimestamp%lld", client_id, name, product_key, ts);
    //}

    HR_LOGD("iot_client_id:%s\n", iot_client_id);
    HR_LOGD("iot_username:%s\n", iot_username);
    HR_LOGD("iot_content:%s\n", iot_content);
    HR_LOGD("iot_content:%s\n", iot_content);

    // iot_username = "LC123456789&a1z1g0btxvW";
    // iot_client_id = "a1z1g0btxvW.LC123456789|securemode=2,signmethod=hmacsha256,timestamp=1745406478068|";

    printf("%s(%d): ............client id:%s\n", __FUNCTION__, __LINE__, iot_client_id);
    iot = mosquitto_iot_new(iot_client_id);

    printf("%s(%d): ............\n", __FUNCTION__, __LINE__);

    if (!iot) {
        printf("%s(%d): ............\n", __FUNCTION__, __LINE__);
        return -1;
    }

    printf("%s(%d): ............\n", __FUNCTION__, __LINE__);

    HR_LOGD("%s(%d): ....hmac_secret:%s\nn", __FUNCTION__, __LINE__, hmac_secret);
    HMAC(EVP_sha256(), hmac_secret, strlen(hmac_secret), (unsigned char*)iot_content, strlen(iot_content), result, &len);

    printf("%s(%d): ............\n", __FUNCTION__, __LINE__);
    for (unsigned int i = 0; i < len; i++) {
        sprintf(iot_password + i * 2, "%02x", result[i]);
    }
    printf("%s(%d): ............\n", __FUNCTION__, __LINE__);
    iot_password[len * 2] = '\0';
    HR_LOGD("iot password:%s\n", iot_password);

    // mosquitto_username_pw_set(iot->mosq, "LC123456789&a1z1g0btxvW" /*iot_username*/ /*_plat.username*/, "d4197fbe38373dc37278aa2be9a86a8431102132d777227f51598bedccebe5d1" /*iot_password*/ /*_plat.password*/);
    mosquitto_username_pw_set(iot->mosq, iot_username, iot_password);

    printf("%s(%d): mosq:%p\n", __FUNCTION__, __LINE__, (void*)iot->mosq);
    // free memory
    free(iot_client_id);
    iot_client_id = NULL;
    free(iot_username);
    iot_username = NULL;
    free(iot_content);
    iot_content = NULL;

    do {
        HR_LOGD("%s(%d): connect:%s:%d\n", __FUNCTION__, __LINE__, _plat.conf.broker.server, _plat.conf.broker.port);
        // 我们发现我电脑 apt 安装的 mosquitto 使用异步连接阿里 iot 的时候总是连接不上,但是 sync 接口测试正常
        // 后来使用自己编译的 mosquitto 测试正常
        // rc = mosquitto_connect_bind_async(iot->mosq, _plat.conf.broker.server, _plat.conf.broker.port,
        //_plat.alive_time, NULL);

        rc = mosquitto_connect(iot->mosq, _plat.conf.broker.server, _plat.conf.broker.port, _plat.alive_time);
        if (rc != MOSQ_ERR_SUCCESS)
            usleep(1000 * 1000);
    } while (rc != MOSQ_ERR_SUCCESS);

    iot->sock = mosquitto_socket(iot->mosq);
    HR_LOGD("new sock:%d\n", iot->sock);

    uv_poll_init(_plat.loop, &iot->poll, iot->sock);
    iot->poll.data = iot;
    iot->pevents = DEFAULT_POLL_EVENTS;
    uv_poll_start(&iot->poll, iot->pevents, _mosquitto_loop_poll_cb);

    uv_timer_init(_plat.loop, &iot->timer);
    iot->timer.data = iot;
    uv_timer_start(&iot->timer, _mosquitto_loop_misc_timer_cb, 1000, 1000);

    _plat.mosq_have_connected = 1;

    _plat.iot = iot;

    uv_run(_plat.loop, UV_RUN_DEFAULT);

    // mosquitto_destroy(_plat.mosq);
    mosquitto_iot_free(iot);
    _plat.iot = NULL;
    mosquitto_lib_cleanup();

    {
        struct dm__topic *n, *p;
        hr_list_for_each_entry_safe(p, n, &_topic_list, entry) {
            dm__topic_free(p);
        }
    }

    // free config
    if (_plat.conf.device.product_key) {
        free(_plat.conf.device.product_key);
        _plat.conf.device.product_key = NULL;
    }

    if (_plat.conf.device.name) {
        free(_plat.conf.device.name);
        _plat.conf.device.name = NULL;
    }

    if (_plat.conf.device.secret) {
        free(_plat.conf.device.secret);
        _plat.conf.device.secret = NULL;
    }

    uv_loop_close(_plat.loop);
    MAKE_VALGRIND_HAPPY(_plat.loop);
    return 0;
}

// implement dm_topic.h
int dm_topic_register(const struct dm_topic* topic) {
    struct dm__topic* t = dm__topic_new(topic);

    return (t != NULL) ? 0 : -1;
}
