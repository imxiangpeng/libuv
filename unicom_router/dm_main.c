#include <json-c/json_object.h>
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

        HR_LOGD("%s(%d): now: %s\n", __FUNCTION__, __LINE__, tmp);

        struct dm_object* dm = dm_object_lookup(tmp, NULL);
        if (!dm) {
            HR_LOGD("%s(%d): can not found %s\n", __FUNCTION__, __LINE__, tmp);
            continue;
        }

        saveptr = tmp;
        while ((token = strtok_r(saveptr, ".", &saveptr))) {
            HR_LOGD("%s(%d): token %s, saveptr:%s(%d)\n", __FUNCTION__, __LINE__, token, saveptr, strlen(saveptr));
            struct json_object* object = NULL;
            if (!json_object_object_get_ex(parent, token, &object)) {
                HR_LOGD("%s(%d): not found object %s, create it\n", __FUNCTION__, __LINE__, token);
                // this is last node/leaf
                if (strlen(saveptr) == 0) {
                    struct dm_value val;

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
#if 0
    for (int i = 0; param[i] != NULL; i++) {
        char tmp[256] = {0};
        const char* p = param[i];

        char *token = NULL, *saveptr = NULL;

        struct json_object* parent = parameterValues;

        memcpy(tmp, param[i], strlen(param[i]));

        HR_LOGD("%s(%d): now: %s\n", __FUNCTION__, __LINE__, tmp);

        struct dm_object* dm = dm_object_lookup(tmp, NULL);
        if (!dm) {
            HR_LOGD("%s(%d): can not found %s\n", __FUNCTION__, __LINE__, tmp);
            continue;
        }

        saveptr = tmp;
        while ((token = strtok_r(saveptr, ".", &saveptr))) {
            HR_LOGD("%s(%d): token %s, saveptr:%s(%d)\n", __FUNCTION__, __LINE__, token, saveptr, strlen(saveptr));
            struct json_object* object = NULL;
            if (!json_object_object_get_ex(parent, token, &object)) {
                HR_LOGD("%s(%d): not found object %s, create it\n", __FUNCTION__, __LINE__, token);
                // this is last node/leaf
                if (strlen(saveptr) == 0) {
                    struct dm_value val;

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
#endif

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

    
    json_object_put(root);

    return 0;
}

static int X_CU_CUEI_getter(struct dm_object* self, struct dm_value* val) {
    HR_LOGD("%s(%d): 22222222222 this %p, val:%p\n", __FUNCTION__, __LINE__, self, val);
    dm_value_set_string_ext(val, "R603H", 1);
    // dm_value_set_string(val, "1234567890");
    return 0;
}

int main(int argc, char** argv) {
    struct hrbuffer b;

    struct url_request* req = NULL;

    memset((void*)&_platform, 0, sizeof(_platform));

    _platform.id = 1;

    hrbuffer_alloc(&b, 512);

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

    uc_platform_stage_1_devauth(&_platform);
    uc_platform_stage_2_getmqttserver(&_platform);
    return 0;
}