// mxp, 20250428, mqtt to houqi platform

#define _GNU_SOURCE

#include "elevator.h"

#include <assert.h>
#include <string.h>
#include "iot.h"
#include "uviot.h"

#include <stdio.h>

#include <mosquitto.h>

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <uv.h>

#include "sconf.h"

#include "iot_topic.h"

#include "hr_log.h"

//#define HQLIFTD_CONF_PATH "/etc/hqliftd/hqliftd.conf"
#define HQLIFTD_CONF_PATH "hqliftd.conf"

#define BROKER_DEFAULT_SERVER "mq.hqszjs.com"
#define BROKER_DEFAULT_PORT 1883     // 8883 //1883
#define BROKER_DEFAULT_ALIVETIME 60  // 300 //60                       // 60s

enum {
    FIELD_SERVER = 0,
    FIELD_PORT,
    FIELD_KEEPALIVE,
    FIELD_USERNAME,
    FIELD_PASSWORD
};

struct sconf_proto _mqtt_conf_fields[] = {
    [FIELD_SERVER] = {"MQ_SERVER", PROTO_VALUE_STRING, {.string = NULL}},
    [FIELD_PORT] = {"MQ_PORT", PROTO_VALUE_INT64, {.int64 = BROKER_DEFAULT_PORT}},
    [FIELD_KEEPALIVE] = {"MQ_KEEPALIVE", PROTO_VALUE_INT64, {.int64 = BROKER_DEFAULT_ALIVETIME}},
    [FIELD_USERNAME] = {"MQ_USERNAME", PROTO_VALUE_STRING, {.string = NULL}},
    [FIELD_PASSWORD] = {"MQ_PASSWORD", PROTO_VALUE_STRING, {.string = NULL}},
};

static struct uviot* _iot = NULL;

int iot_init(struct uv_loop_s* loop) {

    _iot = uviot_alloc(loop);
    if (!_iot)
        return -1;

    if (0 == sconf_load_with_proto(HQLIFTD_CONF_PATH, _mqtt_conf_fields, sizeof(_mqtt_conf_fields) / sizeof(_mqtt_conf_fields[0]))) {
        if (NULL != _mqtt_conf_fields[FIELD_SERVER].value.string) {
            snprintf(_iot->server, sizeof(_iot->server), "%s", _mqtt_conf_fields[FIELD_SERVER].value.string);
            free(_mqtt_conf_fields[FIELD_SERVER].value.string);
            _mqtt_conf_fields[FIELD_SERVER].value.string = NULL;
        } else {
            snprintf(_iot->server, sizeof(_iot->server), "%s", BROKER_DEFAULT_SERVER);
        }

        _iot->port = _mqtt_conf_fields[FIELD_PORT].value.int64;
        _iot->alive_time = _mqtt_conf_fields[FIELD_KEEPALIVE].value.int64;

        snprintf(_iot->id, sizeof(_iot->id), "%s", elevator_serialno());

        if (NULL != _mqtt_conf_fields[FIELD_USERNAME].value.string) {
            snprintf(_iot->username, sizeof(_iot->username), "%s", _mqtt_conf_fields[FIELD_USERNAME].value.string);
            free(_mqtt_conf_fields[FIELD_USERNAME].value.string);
            _mqtt_conf_fields[FIELD_USERNAME].value.string = NULL;
        } else {
            snprintf(_iot->username, sizeof(_iot->username), "%s", "inspur");
        }
        if (NULL != _mqtt_conf_fields[FIELD_PASSWORD].value.string) {
            snprintf(_iot->password, sizeof(_iot->password), "%s", _mqtt_conf_fields[FIELD_PASSWORD].value.string);
            free(_mqtt_conf_fields[FIELD_PASSWORD].value.string);
            _mqtt_conf_fields[FIELD_PASSWORD].value.string = NULL;
        } else {
            snprintf(_iot->password, sizeof(_iot->password), "%s", "inspur88*");
        }
    } else {
        snprintf(_iot->server, sizeof(_iot->server), "%s", BROKER_DEFAULT_SERVER);
        _iot->port = BROKER_DEFAULT_PORT;
        _iot->alive_time = BROKER_DEFAULT_ALIVETIME;

        snprintf(_iot->id, sizeof(_iot->id), "%s", elevator_serialno());
        snprintf(_iot->username, sizeof(_iot->username), "%s", "inspur");
        snprintf(_iot->password, sizeof(_iot->password), "%s", "inspur88*");
    }

    HR_LOGD("server:%s:%d, alive:%d seconds, client id:%s, username:%s, password:%s\n", _iot->server, _iot->port, _iot->alive_time, _iot->id, _iot->username, _iot->password);
    // topic init early, we can observe motion event early before motion started
    iot_topic_init(_iot, "", "");

    uviot_prepare(_iot);

    return 0;
}

int iot_deinit(void) {
    uviot_release(_iot);
    _iot = NULL;
    return 0;
}
