#include <json-c/json_object.h>
#include <mosquitto.h>
#include <stdio.h>
#include <string.h>

#include "dm_Device.h"
#include "dm_object.h"
#include "hr_buffer.h"
#include "hr_log.h"
#include "json-c/json.h"
#include "url_request.h"

#define MNG_URL "https://123.6.50.69:8803"

#define MNG_URL_AUTH MNG_URL "/api/auth"
#define MNG_URL_DEVICE_GET_MQTT_SERVER MNG_URL "/api/deviceGetMqttServer"

#ifndef UNUSED
#define UNUSED(x) (void)(x)
#endif

struct uc_platform {
    char broker[256];
    int port;
    int alive_time;  // 60s
    char username[256];
    char password[256];
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

    struct mosquitto* mosq;
};

static struct uc_platform _platform = {0};

const char* _stage_1_devauth_parameters[] = {
    "Device.DeviceInfo.ModelName",
    "Device.DeviceInfo.SerialNumber",
    "Device.DeviceInfo.SoftwareVersion",
    "Device.DeviceInfo.X_CU_FriendlyName",
    "Device.X_CU_WAN.ConnectionType",
    "Device.DeviceInfo.X_CU_CUEI",
    "Device.X_CU_WAN.MAC",
    NULL};

const char* _stage_2_getmqttserver_parameters[] = {
    "Device.DeviceInfo.SerialNumber",
    "Device.DeviceInfo.ManufacturerOUI",
    "Device.DeviceInfo.X_CU_CUEI",
    NULL};

static int _fill_parameters(const char* param[], struct json_object* parameter_values) {
    if (!param || !parameter_values) return -1;

    for (int i = 0; param[i] != NULL; i++) {
        char tmp[256] = {0};
        const char* p = param[i];

        char *token = NULL, *saveptr = NULL;

        struct json_object* parent = parameter_values;

        memcpy(tmp, param[i], strlen(param[i]));

        struct dm_object* dm = dm_object_lookup(tmp, NULL);
        if (!dm) {
            // HR_LOGD("%s(%d): can not found %s\n", __FUNCTION__, __LINE__, tmp);
            continue;
        }

        saveptr = tmp;
        while ((token = strtok_r(saveptr, ".", &saveptr))) {
            // HR_LOGD("%s(%d): token %s, saveptr:%s(%d)\n", __FUNCTION__, __LINE__, token, saveptr, strlen(saveptr));
            struct json_object* object = NULL;
            if (!json_object_object_get_ex(parent, token, &object)) {
                // this is last node/leaf
                if (strlen(saveptr) == 0) {
                    struct dm_value val;

                    memset((void*)&val, 0, sizeof(val));
                    dm->getter(dm, &val);
                    if (val.type == DM_TYPE_NUMBER) {
                        json_object_object_add(parent, token, json_object_new_int(val.val.number));
                    } else if (val.type == DM_TYPE_STRING) {
                        json_object_object_add(parent, token, json_object_new_string(val.val.string ? val.val.string : ""));
                    }
                } else {
                    object = json_object_new_object();
                    json_object_object_add(parent, token, object);
                }
            }

            parent = object;
        }
    }

    return 0;
}

// E.3.2 devauth
int uc_platform_create_stage_1_devauth_message(struct uc_platform* plat, struct hrbuffer* buf, const char* param[]) {
    struct json_object *root = NULL, *result = NULL, *parameterValues = NULL;
    char tmp[64] = {0};
    root = json_object_new_object();

    json_object_object_add(root, "cwmpVersion", json_object_new_string("1.0"));

    snprintf(tmp, sizeof(tmp), "%d", plat->id++);
    json_object_object_add(root, "id", json_object_new_string(tmp));
    json_object_object_add(root, "method", json_object_new_string("devAuth"));

    json_object_object_add(root, "devId", json_object_new_string(plat->device_id));
    json_object_object_add(root, "secret", json_object_new_string(plat->secret));

    json_object_object_add(root, "accType", json_object_new_string("router"));

    parameterValues = json_object_new_object();
    json_object_object_add(root, "parameterValues", parameterValues);

    _fill_parameters(param, parameterValues);
    HR_LOGD("%s(%d): message object %s\n", __FUNCTION__, __LINE__, json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN));

    hrbuffer_append_string(buf, json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN));

    json_object_put(root);
    return 0;
}

// E.3.3 getMqttServer
int uc_platform_create_stage_2_getmqttserver_message(struct uc_platform* plat, struct hrbuffer* buf, const char* param[]) {
    struct json_object *root = NULL, *result = NULL, *parameterValues = NULL;
    char tmp[64] = {0};
    root = json_object_new_object();

    json_object_object_add(root, "cwmpVersion", json_object_new_string("1.0"));

    snprintf(tmp, sizeof(tmp), "%d", plat->id++);
    json_object_object_add(root, "id", json_object_new_string(tmp));
    json_object_object_add(root, "method", json_object_new_string("devGetMqttServer"));

    json_object_object_add(root, "cookie", json_object_new_string(plat->cookie));

    parameterValues = json_object_new_object();
    json_object_object_add(root, "parameterValues", parameterValues);

    _fill_parameters(param, parameterValues);
    HR_LOGD("%s(%d): message object %s\n", __FUNCTION__, __LINE__, json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN));

    hrbuffer_append_string(buf, json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN));

    json_object_put(root);
    return 0;
}

// E.3.2 /api/auth, devAuth
int uc_platform_stage_1_devauth(struct uc_platform* plat) {
    struct hrbuffer b;
    struct url_request* req = NULL;
    struct json_object *root = NULL, *result = NULL, *cookie = NULL;

    if (!plat) return -1;

    hrbuffer_alloc(&b, 512);
    uc_platform_create_stage_1_devauth_message(plat, &b, _stage_1_devauth_parameters);

    HR_LOGD("request message:%s\n", b.data);
    req = url_request_new();
    req->set_header(req, "Content-Type", "application/json;charset=UTF-8");

    req->set_form(req, b.data, NULL);

    hrbuffer_reset(&b);
    req->post(req, MNG_URL_AUTH, &b);
    url_request_free(req);

    printf("response:%s\n", b.data);
    root = json_tokener_parse(b.data);
    hrbuffer_free(&b);
    if (!root) {
        HR_LOGD(" can not parse json .....\n");
        return -1;
    }

    json_object_object_get_ex(root, "result", &result);
    if (!result || json_object_get_int(result) != 0) {
        json_object_put(root);
        HR_LOGD(" can not parse json .....\n");
        return -1;
    }
    json_object_object_get_ex(root, "cookie", &cookie);
    if (!cookie) {
        json_object_put(root);
        HR_LOGD(" can not parse json .....\n");
        return -1;
    }

    const char* str = json_object_get_string(cookie);
    memcpy(plat->cookie, str, strlen(str));
    json_object_put(root);

    return 0;
}
// E.3.3 /api/deviceGetMqttServer , devGetMqttServer
int uc_platform_stage_2_getmqttserver(struct uc_platform* plat) {
    struct hrbuffer b;
    struct url_request* req = NULL;
    struct json_object *root = NULL, *result = NULL, *cookie = NULL;

    if (!plat) return -1;

    hrbuffer_alloc(&b, 512);

    uc_platform_create_stage_2_getmqttserver_message(plat, &b, _stage_2_getmqttserver_parameters);

    HR_LOGD("request message:%s\n", b.data);
    req = url_request_new();
    req->set_header(req, "Content-Type", "application/json;charset=UTF-8");

    req->set_form(req, b.data, NULL);

    hrbuffer_reset(&b);
    req->post(req, MNG_URL_DEVICE_GET_MQTT_SERVER, &b);
    url_request_free(req);

    printf("response:%s\n", b.data);
    root = json_tokener_parse(b.data);
    hrbuffer_free(&b);
    if (!root) {
        HR_LOGD(" can not parse json .....\n");
        return -1;
    }

    json_object_object_get_ex(root, "result", &result);
    if (!result || json_object_get_int(result) != 0) {
        json_object_put(root);
        HR_LOGD(" can not parse json .....\n");
        return -1;
    }
    json_object_object_get_ex(root, "cookie", &cookie);
    if (!cookie) {
        json_object_put(root);
        HR_LOGD(" can not parse json .....\n");
        return -1;
    }

    const char* str = json_object_get_string(cookie);
    if (strcmp(plat->cookie, str)) {
        memset((void*)plat->cookie, 0, sizeof(plat->cookie));
        memcpy(plat->cookie, str, strlen(str));
    }

    const char* uuid = json_object_get_string(json_object_object_get(root, "uuid"));

    struct json_object* mqtt_client_1 = NULL;
    if (!json_object_object_get_ex(json_object_object_get(root, "parameterValues"), "Device.MQTT.Client.1", &mqtt_client_1) || !mqtt_client_1) {
        HR_LOGD(" server response error, can not found Device.MQTT.Client.1 .....\n");
        // return -1;
    }

    const char* broker = json_object_get_string(json_object_object_get(mqtt_client_1, "BrokerAddress"));
    int port = json_object_get_int(json_object_object_get(mqtt_client_1, "BrokerPort"));
    int alive_time = json_object_get_int(json_object_object_get(mqtt_client_1, "KeepAliveTime"));
    const char* X_CU_Topic = json_object_get_string(json_object_object_get(mqtt_client_1, "Subscription.1.Topic"));

    const char* username = json_object_get_string(json_object_object_get(mqtt_client_1, "Username"));
    const char* password = json_object_get_string(json_object_object_get(mqtt_client_1, "Password"));

    HR_LOGD("%s(%d): broker:%s, port:%d, alive time:%d,topic:%s, username:%s, password:%s, uuid:%s\n", __FUNCTION__, __LINE__, broker, port, alive_time, X_CU_Topic, username, password, uuid);

    strcpy(plat->broker, broker);
    plat->port = port;
    plat->alive_time = alive_time;
    strcpy(plat->topic, X_CU_Topic);
    strcpy(plat->username, username);
    strcpy(plat->password, password);

    json_object_put(root);

    return 0;
}

static int X_CU_CUEI_getter(struct dm_object* self, struct dm_value* val) {
    HR_LOGD("%s(%d): 22222222222 this %p, val:%p\n", __FUNCTION__, __LINE__, self, val);
    dm_value_set_string_ext(val, "R603H", 1);
    // dm_value_set_string(val, "1234567890");
    return 0;
}

static void _on_log(struct mosquitto* mosq, void* obj, int level, const char* str) {
    HR_LOGD("%s\n", str);
}

static void _on_connect(struct mosquitto* mosq, void* obj, int result) {
    int i;

    UNUSED(obj);

    HR_LOGD("%s(%d): \n", __FUNCTION__, __LINE__);

    int connack_received = true;

    int connack_result = result;
    if (!result) {
        // mosquitto_subscribe_multiple(mosq, NULL, 1, &_platform.topic, _platform.qos, NULL, NULL);
        mosquitto_subscribe(mosq, NULL, _platform.topic, _platform.qos);
    } else {
        if (result) {
            HR_LOGD("Connection error: %s\n", mosquitto_connack_string(result));
        }
        mosquitto_disconnect_v5(mosq, 0, NULL);
    }
}
static void _on_connect_with_flags(struct mosquitto* mosq, void* obj, int result, int flags) {
    int i;

    UNUSED(obj);
    UNUSED(flags);

    HR_LOGD("%s(%d): \n", __FUNCTION__, __LINE__);

    int connack_received = true;

    int connack_result = result;
    if (!result) {
        mosquitto_subscribe(mosq, NULL, _platform.topic, _platform.qos);
    } else {
        if (result) {
            HR_LOGD("Connection error: %s\n", mosquitto_connack_string(result));
        }
        mosquitto_disconnect_v5(mosq, 0, NULL);
    }
}


static void _on_disconnect(struct mosquitto* mosq, void* userdata, int rc) {
    HR_LOGD("%s(%d): \n", __FUNCTION__, __LINE__);
}

static void _subscribe_callback(struct mosquitto* mosq, void* obj, int mid, int qos_count, const int* granted_qos) {
    int i;
    bool some_sub_allowed = (granted_qos[0] < 128);
    bool should_print = 1;

    HR_LOGD("%s(%d): \n", __FUNCTION__, __LINE__);
    if (should_print) printf("Subscribed (mid: %d): %d", mid, granted_qos[0]);
    for (i = 1; i < qos_count; i++) {
        if (should_print) printf(", %d", granted_qos[i]);
        some_sub_allowed |= (granted_qos[i] < 128);
    }
    if (should_print) printf("\n");

    if (some_sub_allowed == false) {
        mosquitto_disconnect_v5(mosq, 0, NULL);
        HR_LOGD("All subscription requests were denied.\n");
    }
}

static void _on_message(struct mosquitto* mosq, void* obj, const struct mosquitto_message* message) {
    int i;
    bool res;

    UNUSED(obj);

    // if(process_messages == false) return;
    HR_LOGD("%s(%d): receive topic:%s, payloadlen:%s\n", __FUNCTION__, __LINE__, message->topic, message->payloadlen);
}

int main(int argc, char** argv) {
    struct url_request* req = NULL;

    memset((void*)&_platform, 0, sizeof(_platform));

    _platform.id = 1;
    _platform.qos = 0;

    dm_Device_init(NULL);

    struct dm_object* Device = dm_object_lookup("Device", NULL);

    struct dm_object* object = dm_object_lookup("Device.X_CU_LockEnable", NULL);
    if (object) {
        struct dm_value val;
        object->getter(object, &val);
        HR_LOGD("lock:%d\n", val.val.number);
        val.val.number = !val.val.number;
        HR_LOGD("adjust lock:%d\n", val.val.number);
        object->setter(object, &val);
    }

    object = dm_object_lookup("X_CU_LockEnable", Device);
    if (object) {
        struct dm_value val;
        object->getter(object, &val);
        HR_LOGD("direct query from Device lock:%d\n", val.val.number);
    }

    object = dm_object_lookup("Device.DeviceInfo.SerialNumber", NULL);
    if (object) {
        struct dm_value val;
        object->getter(object, &val);
        HR_LOGD("serial:%s\n", val.val.string);
        dm_value_reset(&val);
    }

    object = dm_object_lookup("Device.DeviceInfo.ModelName", NULL);
    if (object) {
        struct dm_value val;
        object->getter(object, &val);
        HR_LOGD("model 1 :%s\n", val.val.string);
        dm_value_reset(&val);
    }

    dm_object_new_ext("Device.DeviceInfo.ModelName2", DM_TYPE_STRING, X_CU_CUEI_getter, NULL);

    object = dm_object_lookup("Device.DeviceInfo.ModelName2", NULL);
    if (object) {
        struct dm_value val;
        object->getter(object, &val);
        HR_LOGD("model 2:%s\n", val.val.string);
        dm_value_reset(&val);
    }

    object = dm_object_lookup("Device.DeviceInfo.", NULL);
    if (object) {
        struct dm_value val;
        object->getter(object, &val);
        HR_LOGD("Device.DeviceInfo:%s\n", val.val.string);
        dm_value_reset(&val);
    }

    uc_platform_stage_1_devauth(&_platform);
    uc_platform_stage_2_getmqttserver(&_platform);

    object = dm_object_lookup("Device.DeviceInfo.X_CU_CUEI", NULL);
    if (object) {
        struct dm_value val;
        object->getter(object, &val);
        // using cuei as mqtt's client id
        strcpy(_platform.client_id, val.val.string);
        dm_value_reset(&val);
    }

    HR_LOGD("%s(%d): try connect mqtt :%s:%d\n", __FUNCTION__, __LINE__, _platform.broker, _platform.port);
    mosquitto_lib_init();

    _platform.mosq = mosquitto_new(_platform.client_id, false, &_platform);
    if (!_platform.mosq) {
        HR_LOGD("%s(%d): error can not instance mosquitto\n", __FUNCTION__, __LINE__);
        dm_object_free(NULL);
        return -1;
    }

    mosquitto_log_callback_set(_platform.mosq, _on_log);

    mosquitto_subscribe_callback_set(_platform.mosq, _subscribe_callback);
    mosquitto_connect_callback_set(_platform.mosq, _on_connect);
    mosquitto_connect_with_flags_callback_set(_platform.mosq, _on_connect_with_flags);
	mosquitto_disconnect_callback_set(_platform.mosq, _on_disconnect);
    mosquitto_message_v5_callback_set(_platform.mosq, _on_message);

    mosquitto_tls_opts_set(_platform.mosq, 0 /*SSL_VERIFY_NONE*/, NULL, NULL);

    const char* cafile = "/home/alex/workspace/workspace/libuv/mqtt_cacert.pem";

    mosquitto_tls_set(_platform.mosq, cafile, NULL, NULL, NULL, NULL);

    mosquitto_tls_insecure_set(_platform.mosq, 0);

    mosquitto_tls_opts_set(_platform.mosq, 0, NULL, NULL);
    // mosquitto_connect_bind_v5(_platform.mosq, _platform.broker, _platform.port, _platform.alive_time, NULL, NULL);
    mosquitto_connect_bind(_platform.mosq, _platform.broker, _platform.port, _platform.alive_time, NULL);

    mosquitto_loop_forever(_platform.mosq, -1, 1);

    mosquitto_destroy(_platform.mosq);
    _platform.mosq = NULL;

    mosquitto_lib_cleanup();
    dm_object_free(NULL);

    return 0;
}
