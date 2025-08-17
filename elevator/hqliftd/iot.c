// mxp, 20250428, mqtt to houqi platform

#define _GNU_SOURCE

#include "iot.h"

#include <assert.h>
#include <mosquitto.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <stdio.h>
#include <string.h>
#include <uv.h>

#include "hr_log.h"
#include "iot_topic.h"
#include "option.h"
#include "uviot.h"

static struct uviot* _iot = NULL;

int iot_init(struct uv_loop_s* loop) {
    _iot = uviot_alloc(loop);
    if (!_iot)
        return -1;

    snprintf(_iot->server, sizeof(_iot->server), "%s", _options[OPTION_MQ_SERVER].value.string);

    _iot->port = _options[OPTION_MQ_PORT].value.number;
    _iot->alive_time = _options[OPTION_MQ_KEEPALIVE].value.number;

    snprintf(_iot->id, sizeof(_iot->id), "%s", _options[OPTION_MQ_ID].value.string /*elevator_serialno()*/);

    snprintf(_iot->username, sizeof(_iot->username), "%s", _options[OPTION_MQ_USERNAME].value.string);
    snprintf(_iot->password, sizeof(_iot->password), "%s", _options[OPTION_MQ_PASSWORD].value.string);

    // we should use tls/ssl protocol, special cafile or capath
    if (0 == strcasecmp("mqtts", _options[OPTION_MQ_PROTO].value.string)) {

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
    iot_topic_deinit();
    return 0;
}
