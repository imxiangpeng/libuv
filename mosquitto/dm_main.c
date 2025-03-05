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

struct dm_platform {
    struct url_request *req;
    char host[256];
    int port;
    int alive_time;  // 60s
    char username[256];
    char password[256];
    char uuid[128];
    // 1 -> http /api/auth
    // 2 -> http /api/auth
    // 3 -> http get mqtt server
    // 4 -> mqtt
    int stage;

    int id;  // sequence id, start from 1
    char cookie[128];

    char device_id[128];
    char secret[128];
    char client_id[128];
    int qos;

    char topic[256];
    int topic_mid;

    struct mosquitto *mosq;

    pthread_t tid;

    int login_after_exit;  // after bind active we should login again
};

static struct dm_platform _platform = {0};
// static int _dm_action_command_sendstate(struct dm_platform *plat, struct json_object *root);
// static int _dm_action_command_sendvideo(struct dm_platform *plat, struct json_object *root);
// static int _dm_action_command_videoplayback(struct dm_platform *plat, struct json_object *root);
// static int _dm_action_command_uploadrecord(struct dm_platform *plat, struct json_object *root);
// static int _dm_action_command_recorddownload(struct dm_platform *plat, struct json_object *root);

// // actions of /API/V1/Down/{设备序列号}/Command

// static struct dm_action _dm_action_command_tbl[] = {
//     {"Sendstate", _dm_action_command_sendstate},
//     {"Sendvideo", _dm_action_command_sendvideo},
//     {"videoPlayBack", _dm_action_command_videoplayback},
//     {"uploadRecord", _dm_action_command_uploadrecord},
//     {"recordDownload", _dm_action_command_recorddownload},
// };

// notify callback function, trigger publish topic
// void dm_on_publish(const char* model) {
// }
struct dm__topic {
    int mid;
    const struct dm_topic *self;
    struct hr_list_head entry;
};

static HR_LIST_HEAD(_topic_list);
// // list of topics requiring subscription
// // /API/V1/Down/{设备序列号}/{topic}
// static struct dm__topic _dm_subscribe_topic_tbl[] = {
//     {"/API/V1/Down/%s/Command", "Command"},
// };
// static struct dm__topic _dm_publish_topic_tbl[] = {
//     {"/API/V1/Up/HeartBeat", "HeartBeat"},
//     {"/API/V1/Up/LiftState", "LiftState"},
//     {"/API/V1/Up/LiftFault", "LiftFault"},
//     {"/API/V1/Up/LiftRunInfo", "LiftRunInfo"},
// };

static struct dm__topic *dm__topic_new(const struct dm_topic *topic) {
    struct dm__topic *t = NULL;
    if (!topic)
        return NULL;

    t = (struct dm__topic *)calloc(1, sizeof(struct dm__topic));
    if (!t) {
        return NULL;
    }

    t->self = topic;

    HR_INIT_LIST_HEAD(&t->entry);

    hr_list_add_tail(&t->entry, &_topic_list);

    return t;
}

static void dm__topic_free(struct dm__topic *t) {
    if (!t) {
        return;
    }

    // no lock ...
    hr_list_del(&t->entry);

    HR_INIT_LIST_HEAD(&t->entry);

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
                int ret = mosquitto_publish(mosq, &p->mid, p->self->topic,
                                            len, (const void *)payload,
                                            0, false);
                free(payload);
                HR_LOGD("%s(%d): connected, auto publish:%s -> (%d)\n", __FUNCTION__, __LINE__, p->self->topic, ret);
            }
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
}

static void _on_subscribe(struct mosquitto *mosq, void *obj, int mid,
                          int qos_count, const int *granted_qos) {
    int i;
    bool some_sub_allowed = (granted_qos[0] < 128);
    bool should_print = 1;

    struct dm_platform *plat = (struct dm_platform *)obj;

    if (!plat) return;

    HR_LOGD("%s(%d): mid:%d vs %d\n", __FUNCTION__, __LINE__, mid, plat->topic_mid);

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
    mosquitto_disconnect(_platform.mosq);
    mosquitto_loop_stop(_platform.mosq, 1);
    }
}

static int _load_conf(struct dm_platform *plat) {
    size_t len = 0;
    char *data = NULL;

    const char* path = DM_DEFAULT_CONFIG_DIR "/" DM_DEFAULT_CONFIG_NAME;
    if (!plat) return -1;

    len = _read_file(path, &data);
    if (!data) {
        return -1;
    }

    // unlink(path);

    //{"account":"01K0Nya5SYN97V5JzWURwvAg==","code":"Tkwqv8UdxKQQ481","devId":"200037050001000","psk":"router"}

    cJSON *root = cJSON_ParseWithLength(data, len);
    free(data);
    data = NULL;

    if (!root) return -1;

    const char *username = cJSON_GetStringValue(cJSON_GetObjectItem(root, "username"));
    const char *password = cJSON_GetStringValue(cJSON_GetObjectItem(root, "password"));
    const char *client_id= cJSON_GetStringValue(cJSON_GetObjectItem(root, "id"));
    const char *broker = cJSON_GetStringValue(cJSON_GetObjectItem(root, "server"));
    double port = cJSON_GetNumberValue(cJSON_GetObjectItem(root, "port"));

    if (isnan((double)port)) {
        port = BROKER_DEFAULT_PORT;
    } else {
        _platform.port = (int)port;
    }

    printf("broker:%s, client_id:%s, username:%s, password:%s\n", broker, client_id, username, password);

    snprintf(_platform.username, sizeof(_platform.username), "%s", username);
    snprintf(_platform.password, sizeof(_platform.password), "%s", password);
    snprintf(_platform.host, sizeof(_platform.host), "%s", broker);
    snprintf(_platform.client_id, sizeof(_platform.client_id), "%s", client_id);
    cJSON_Delete(root);

    return 0;
}

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

    memset((void *)&_platform, 0, sizeof(_platform));

    _platform.id = 1;
    _platform.qos = 0;
    _platform.tid = 0;
    
    _platform.alive_time = 60;
    _platform.port = 1883;

    snprintf(_platform.username, sizeof(_platform.username), "%s", "who");
    snprintf(_platform.password, sizeof(_platform.password), "%s", "me");

    _load_conf(&_platform);


    HR_LOGD("%s(%d): try connect mqtt :%s:%d\n", __FUNCTION__, __LINE__,
            _platform.host, _platform.port);
    

    dm_topic_init();

    mosquitto_lib_init();

    HR_LOGD("client id:%s\n", _platform.client_id);
    _platform.mosq = mosquitto_new(_platform.client_id, false, &_platform);
    if (!_platform.mosq) {
        HR_LOGD("%s(%d): error can not instance mosquitto\n", __FUNCTION__,
                __LINE__);
        return -1;
    }

    mosquitto_log_callback_set(_platform.mosq, _on_log);

    mosquitto_subscribe_callback_set(_platform.mosq, _on_subscribe);
    mosquitto_connect_callback_set(_platform.mosq, _on_connect);
    // mosquitto_connect_with_flags_callback_set(_platform.mosq,
    // _on_connect_with_flags);
    mosquitto_disconnect_callback_set(_platform.mosq, _on_disconnect);
    mosquitto_message_callback_set(_platform.mosq, _on_message);

    mosquitto_publish_callback_set(_platform.mosq, _on_publish);

    mosquitto_tls_opts_set(_platform.mosq, 0 /*SSL_VERIFY_NONE*/, NULL, NULL);

    // const char *cafile = "/home/alex/workspace/workspace/libuv/mqtt_cacert.pem";
    // mosquitto_tls_set(_platform.mosq, cafile, NULL, NULL, NULL, NULL);
    mosquitto_tls_insecure_set(_platform.mosq, 0);
    mosquitto_tls_opts_set(_platform.mosq, 0, NULL, NULL);

    mosquitto_username_pw_set(_platform.mosq, _platform.username, _platform.password);

    do {
        HR_LOGD("%s(%d): connect:%s:%d\n", __FUNCTION__, __LINE__, _platform.host, _platform.port);
        rc = mosquitto_connect_bind(_platform.mosq, _platform.host, _platform.port,
                                    _platform.alive_time, NULL);
        usleep(1000 * 2000);
    } while (rc != MOSQ_ERR_SUCCESS);

    mosquitto_loop_forever(_platform.mosq, -1, 1);

    mosquitto_destroy(_platform.mosq);
    _platform.mosq = NULL;

    mosquitto_lib_cleanup();

    {
        struct dm__topic *n, *p;
        hr_list_for_each_entry_safe(p, n, &_topic_list, entry) {
            dm__topic_free(p);
        }
    }

    return 0;
}

// implement dm_topic.h
int dm_topic_register(const struct dm_topic *topic) {
    struct dm__topic *t = dm__topic_new(topic);

    return (t != NULL) ? 0 : -1;
}
