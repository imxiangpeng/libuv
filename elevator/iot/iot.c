#define _GNU_SOURCE
#include "iot.h"
#include <string.h>

#include <stdio.h>

#include <mosquitto.h>

#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <uv.h>

#include "hr_log.h"
#include "iot_mosquitto.h"

#define BROKER_DEFAULT_SERVER "a1z1g0btxvW.iot-as-mqtt.cn-shanghai.aliyuncs.com"
#define BROKER_DEFAULT_PORT 1883     // 8883 //1883
#define BROKER_DEFAULT_ALIVETIME 60  // 300 //60                       // 60s
// https://living.aliyun.com/project/a123Vlj9ublcLvZq/dev/
#define TIHUIYAN_PRODUCT_KEY "a1z1g0btxvW"
#define TIHUIYAN_DEVICE_SECRET "130cdc9746be2eeaad3ca8caaf989137"
#define TIHUIYAN_DEVICE_NAME "LC123456789"

static struct iot* _iot = NULL;

static long long time_ms() {
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) == -1) {
        return 0;
    }
    return (ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL);
}

int iot_init() {
    char* hmac_secret = NULL;

    char* iot_content = NULL;
    unsigned char result[EVP_MAX_MD_SIZE] = {0};
    unsigned int len = EVP_MAX_MD_SIZE;

    char* name = TIHUIYAN_DEVICE_NAME;
    char* client_id = TIHUIYAN_PRODUCT_KEY "." TIHUIYAN_DEVICE_NAME;
    char* product_key = TIHUIYAN_PRODUCT_KEY;

    long long ts = time_ms();

    mosquitto_lib_init();

    _iot = iot_mosquitto_alloc();
    if (!_iot)
        return -1;

    asprintf(&_iot->server, "%s", BROKER_DEFAULT_SERVER);
    _iot->port = BROKER_DEFAULT_PORT;
    
    hmac_secret = TIHUIYAN_DEVICE_SECRET;
    // mqttClientId: clientId+"|securemode=3,signmethod=hmacsha1,timestamp=132323232|"
    // mqttUsername: deviceName+"&"+productKey
    // mqttPassword: sign_hmac(deviceSecret,content)
    asprintf(&_iot->id, "%s|securemode=2,signmethod=hmacsha256,timestamp=%lld|", client_id, ts);
    // {deviceName}&{productKey}
    asprintf(&_iot->username, "%s&%s", name, product_key);
    // "clientId" + {ClientId}+ "deviceName" + {deviceName }+ "productKey" + {productKey }+ "timestamp" + {timestamp}
    asprintf(&iot_content, "clientId%sdeviceName%sproductKey%stimestamp%lld", client_id, name, product_key, ts);

    HR_LOGD("%s(%d): ....hmac_secret:%s\nn", __FUNCTION__, __LINE__, hmac_secret);
    HMAC(EVP_sha256(), hmac_secret, strlen(hmac_secret), (unsigned char*)iot_content, strlen(iot_content), result, &len);

    _iot->password = (char*)calloc(1, EVP_MAX_MD_SIZE * 2 + 1);
    if (!_iot->password) {
        free(_iot->id);
        _iot->id = NULL;
        free(_iot->username);
        _iot->username = NULL;
        free(iot_content);
        return -1;
    }
    for (unsigned int i = 0; i < len; i++) {
        sprintf(_iot->password + i * 2, "%02x", result[i]);
    }
    _iot->password[len * 2] = '\0';
    HR_LOGD("iot password:%s\n", _iot->password);

    // free memory
    free(iot_content);
    iot_content = NULL;

    // topic init early, we can observe motion event early before motion started
    iot_topic_init(product_key, name);
    return 0;
}

int iot_run(uv_loop_t* loop) {

    HR_LOGD("%s(%d): .........\n", __FUNCTION__, __LINE__);
    iot_mosquitto_prepare(_iot);

    iot_mosquitto_run(_iot, loop);
    return 0;
}

int iot_deinit(void) {
    HR_LOGD("%s(%d): .........\n", __FUNCTION__, __LINE__);
    iot_mosquitto_release(_iot);
    HR_LOGD("%s(%d): .........\n", __FUNCTION__, __LINE__);
    _iot = NULL;
    HR_LOGD("%s(%d): .........\n", __FUNCTION__, __LINE__);
    mosquitto_lib_cleanup();
    HR_LOGD("%s(%d): .........\n", __FUNCTION__, __LINE__);
    return 0;
}

// impl
int iot_topic_register(const struct iot_topic* topic) {
    return iot_mosquitto_topic_register(_iot, topic);
}

int iot_topic_public_async(const struct iot_topic* topic) {

    return iot_mosquitto_public_async(_iot,topic);
}