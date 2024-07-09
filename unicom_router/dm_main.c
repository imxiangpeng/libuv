#include <json-c/json.h>
#include <limits.h>
#include <mosquitto.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "dm_Device.h"
#include "dm_FactoryReset.h"
#include "dm_Reboot.h"
#include "hr_buffer.h"
#include "hr_list.h"
#include "hr_log.h"
#include "url_request.h"

#define MNG_URL "https://123.6.50.69:8803"

#define MNG_URL_AUTH MNG_URL "/api/auth"
#define MNG_URL_DEVICE_GET_MQTT_SERVER MNG_URL "/api/deviceGetMqttServer"

#ifndef UNUSED
#define UNUSED(x) (void)(x)
#endif

#define DM_DATA_FORMAT_UNION 1

struct uc_platform {
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
};

struct uc_dm_action {
    const char *name;
    // json object or string ?
    int (*action)(struct uc_platform *, struct json_object *);
};

typedef int (*uc_on_response)(struct uc_platform *, struct json_object *);

struct uc_message_response {
    int id;  // message id

    uc_on_response response;

    struct hr_list_head entry;
};

static struct uc_platform _platform = {0};

static HR_LIST_HEAD(_message_response_queue);

const char *_stage_1_devauth_parameters[] = {
    "Device.DeviceInfo.ModelName",
    "Device.DeviceInfo.SerialNumber",
    "Device.DeviceInfo.SoftwareVersion",
    "Device.DeviceInfo.X_CU_FriendlyName",
    "Device.X_CU_WAN.ConnectionType",
    "Device.DeviceInfo.X_CU_CUEI",
    "Device.X_CU_WAN.MAC",
    NULL,
};

const char *_stage_2_getmqttserver_parameters[] = {
    "Device.DeviceInfo.SerialNumber",
    "Device.DeviceInfo.ManufacturerOUI",
    "Device.DeviceInfo.X_CU_CUEI",
    NULL,
};

const char *_mqtt_device_info_sync_parameters[] = {
    "Device.DeviceInfo.SerialNumber",
    "Device.DeviceInfo.ModelName",
    "Device.DeviceInfo.X_CU_DeviceType",
    "Device.DeviceInfo.X_CU_ManufacturerCode",
    "Device.DeviceInfo.X_CU_ProductId",
    "Device.DeviceInfo.HardwareVersion",
    "Device.DeviceInfo.SoftwareVersion",
    "Device.DeviceInfo.X_CU_ProtocolType",
    "Device.DeviceInfo.X_CU_CUEI",
    "Device.DeviceInfo.Manufacturer",
    "Device.X_CU_WAN.MAC",
    NULL,
};

const char *_mqtt_device_active_parameters[] = {
    "Device.DeviceInfo.SerialNumber",
    "Device.DeviceInfo.ManufacturerOUI",
    "Device.DeviceInfo.X_CU_CUEI",
    NULL,
};

static int _uc_dm_action_get_parameter_values(struct uc_platform *plat,
                                              struct json_object *root);
static int _uc_dm_action_set_parameter_values(struct uc_platform *plat,
                                              struct json_object *root);
static int _uc_dm_action_add_instance(struct uc_platform *plat,
                                      struct json_object *root);
static int _uc_dm_action_del_instance(struct uc_platform *plat,
                                      struct json_object *root);

static int _uc_dm_action_qos_class_host_op(struct uc_platform *plat,
                                           struct json_object *root);
static int _uc_dm_action_reboot(struct uc_platform *plat, struct json_object *root);

static struct uc_dm_action _uc_dm_action[] = {
    {"getParameterValues", _uc_dm_action_get_parameter_values},
    {"setParameterValues", _uc_dm_action_set_parameter_values},
    {"addInstance", _uc_dm_action_add_instance},
    {"delInstance", _uc_dm_action_del_instance},
    {"addQosClassHost", _uc_dm_action_qos_class_host_op},
    {"setQosClassHost", _uc_dm_action_qos_class_host_op},
    {"delQosClassHost", _uc_dm_action_qos_class_host_op},
    {"reboot", _uc_dm_action_reboot},
    {"restoreConfig", _uc_dm_action_reboot},
};

static int _inc_id(int id) {
    id++;
    if (id == INT_MAX - 1) {
        id = 1;
    }

    return id;
}
#if DM_DATA_FORMAT_UNION
// convert normal json to format: E.10.3's second style
static int _union_parameters(struct json_object *parent, const char *name,
                             struct json_object *object, char *union_name, int union_length) {
    int childs = 0;
    char child_name[128] = {0};

    json_object_object_foreach(object, key, val) {
        snprintf(child_name, sizeof(child_name), "%s", key);
        if (json_object_is_type(val, json_type_object)) {
            _union_parameters(object, key, val, child_name, sizeof(child_name));
        }
        childs++;
    }

    if (childs == 1) {
        if (parent != NULL && name != NULL) {
            char tmp[256] = {0};
            snprintf(tmp, sizeof(tmp), "%s.%s", name, child_name);
            if (union_name) {
                snprintf(union_name, union_length, "%s.%s", name, child_name);
            }
            HR_LOGD("%s(%d): union:%s -> %s\n", __FUNCTION__, __LINE__, name, tmp);
            struct json_object *o = json_object_object_get(object, child_name);
            // mxp, 20240704, must increment reference count, before del!
            json_object_get(o);
            json_object_object_del(object, child_name);
            json_object_object_add(parent, tmp, o);
            json_object_object_del(parent, name);
        }
    }

    return 0;
}
#endif

static int _fill_parameters(const char *param[],
                            struct json_object *parameter_values) {
    if (!param || !parameter_values)
        return -1;

    for (int i = 0; param[i] != NULL; i++) {
        char tmp[256] = {0};
        char *token = NULL, *saveptr = NULL;

        struct json_object *parent = parameter_values;

        memcpy(tmp, param[i], strlen(param[i]));

        struct dm_object *dm = dm_object_lookup(tmp, NULL);
        if (!dm) {
            continue;
        }

        saveptr = tmp;
        while ((token = strtok_r(saveptr, ".", &saveptr))) {
            struct json_object *object = NULL;
            if (!json_object_object_get_ex(parent, token, &object)) {
                // this is last node/leaf
                if (!saveptr || saveptr[0] == '\0') {
                    struct dm_value val;

                    memset((void *)&val, 0, sizeof(val));
                    if (dm->getter)
                        dm->getter(dm, &val);
                    if (val.type == DM_TYPE_NUMBER) {
                        json_object_object_add(parent, token,
                                               json_object_new_int(val.val.number));
                    } else if (val.type == DM_TYPE_STRING) {
                        json_object_object_add(
                            parent, token,
                            json_object_new_string(val.val.string ? val.val.string : ""));
                    }
                } else {
                    object = json_object_new_object();
                    json_object_object_add(parent, token, object);
                }
            }

            parent = object;
        }
    }

#if DM_DATA_FORMAT_UNION
    _union_parameters(NULL, NULL, parameter_values, NULL, 0);
#endif
    return 0;
}

static int _fill_parameters_with_json_array(struct json_object *params,
                                            struct json_object *values) {
    int length = 0;
    if (!params || !values || !json_object_is_type(params, json_type_array))
        return -1;

    length = json_object_array_length(params);

    for (int i = 0; i < length; i++) {
        char tmp[256] = {0};
        const char *p =
            json_object_get_string(json_object_array_get_idx(params, i));

        char *token = NULL, *saveptr = NULL;

        struct json_object *parent = values;

        memcpy(tmp, p, strlen(p));

        struct dm_object *dm = dm_object_lookup(tmp, NULL);
        if (!dm) {
            continue;
        }

        saveptr = tmp;
        while ((token = strtok_r(saveptr, ".", &saveptr))) {
            struct json_object *object = NULL;
            if (!json_object_object_get_ex(parent, token, &object)) {
                // this is last node/leaf
                if (!saveptr || saveptr[0] == '\0') {
                    struct dm_value val;

                    memset((void *)&val, 0, sizeof(val));

                    if (dm->getter)
                        dm->getter(dm, &val);

                    if (val.type == DM_TYPE_NUMBER) {
                        json_object_object_add(parent, token,
                                               json_object_new_int(val.val.number));
                    } else if (val.type == DM_TYPE_STRING) {
                        if (dm->type == DM_TYPE_STRING) {
                            json_object_object_add(
                                parent, token,
                                json_object_new_string(val.val.string ? val.val.string : ""));
                        } else if (dm->type == DM_TYPE_OBJECT) {
                            // care that we should parse json first, should not add string object
                            struct json_object *o = json_tokener_parse(val.val.string);
                            json_object_object_add(parent, token, o);
                        }
                    } else {
                        // not support
                    }

                    dm_value_reset(&val);
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
int uc_platform_create_stage_1_devauth_message(struct uc_platform *plat,
                                               struct hrbuffer *buf,
                                               const char *param[]) {
    struct json_object *root = NULL, *parameterValues = NULL;
    char tmp[64] = {0};
    root = json_object_new_object();

    json_object_object_add(root, "cwmpVersion", json_object_new_string("2.0"));

    snprintf(tmp, sizeof(tmp), "%d", plat->id);
    plat->id = _inc_id(plat->id);
    json_object_object_add(root, "id", json_object_new_string(tmp));
    json_object_object_add(root, "method", json_object_new_string("devAuth"));

    json_object_object_add(root, "devId",
                           json_object_new_string(plat->device_id));
    json_object_object_add(root, "secret", json_object_new_string(plat->secret));

    json_object_object_add(root, "accType", json_object_new_string("router"));

    parameterValues = json_object_new_object();
    json_object_object_add(root, "parameterValues", parameterValues);

    _fill_parameters(param, parameterValues);
    HR_LOGD("%s(%d): message object %s\n", __FUNCTION__, __LINE__,
            json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN));

    hrbuffer_append_string(
        buf, json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN));

    json_object_put(root);
    return 0;
}

// E.3.3 getMqttServer
int uc_platform_create_stage_2_getmqttserver_message(struct uc_platform *plat,
                                                     struct hrbuffer *buf,
                                                     const char *param[]) {
    struct json_object *root = NULL, *parameterValues = NULL;
    char tmp[64] = {0};
    root = json_object_new_object();

    json_object_object_add(root, "cwmpVersion", json_object_new_string("2.0"));

    snprintf(tmp, sizeof(tmp), "%d", plat->id);
    plat->id = _inc_id(plat->id);
    json_object_object_add(root, "id", json_object_new_string(tmp));
    json_object_object_add(root, "method",
                           json_object_new_string("devGetMqttServer"));

    json_object_object_add(root, "cookie", json_object_new_string(plat->cookie));

    parameterValues = json_object_new_object();
    json_object_object_add(root, "parameterValues", parameterValues);

    _fill_parameters(param, parameterValues);

    HR_LOGD("%s(%d): message object %s\n", __FUNCTION__, __LINE__,
            json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN));

    hrbuffer_append_string(
        buf, json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN));

    json_object_put(root);
    return 0;
}

// E.3.2 /api/auth, devAuth
int uc_platform_stage_1_devauth(struct uc_platform *plat) {
    struct hrbuffer b;
    struct url_request *req = plat->req;
    struct json_object *root = NULL, *result = NULL, *cookie = NULL;

    if (!plat)
        return -1;

    hrbuffer_alloc(&b, 512);
    uc_platform_create_stage_1_devauth_message(plat, &b,
                                               _stage_1_devauth_parameters);

    HR_LOGD("request message:%s\n", b.data);
    req->set_header(req, "Content-Type", "application/json;charset=UTF-8");

    req->set_form(req, b.data, NULL);

    hrbuffer_reset(&b);
    req->post(req, MNG_URL_AUTH, &b);

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

    const char *str = json_object_get_string(cookie);
    memcpy(plat->cookie, str, strlen(str));
    json_object_put(root);

    return 0;
}
// E.3.3 /api/deviceGetMqttServer , devGetMqttServer
int uc_platform_stage_2_getmqttserver(struct uc_platform *plat) {
    struct hrbuffer b;
    struct url_request *req = plat->req;
    struct json_object *root = NULL, *result = NULL, *cookie = NULL, *mqtt_client_1 = NULL;
    const char *str = NULL;

    if (!plat)
        return -1;

    hrbuffer_alloc(&b, 512);

    uc_platform_create_stage_2_getmqttserver_message(
        plat, &b, _stage_2_getmqttserver_parameters);

    HR_LOGD("request message:%s\n", b.data);
    req->set_header(req, "Content-Type", "application/json;charset=UTF-8");

    req->set_form(req, b.data, NULL);

    hrbuffer_reset(&b);
    req->post(req, MNG_URL_DEVICE_GET_MQTT_SERVER, &b);

    printf("response:%s\n", b.data);
    root = json_tokener_parse(b.data);
    hrbuffer_free(&b);
    if (!root) {
        HR_LOGD("%s(%d): can not parse json .....\n", __FUNCTION__, __LINE__);
        return -1;
    }

    json_object_object_get_ex(root, "result", &result);
    if (!result || json_object_get_int(result) != 0) {
        json_object_put(root);
        HR_LOGD("%s(%d): can not parse json .....\n", __FUNCTION__, __LINE__);
        return -1;
    }

    json_object_object_get_ex(root, "cookie", &cookie);
    if (!cookie) {
        json_object_put(root);
        HR_LOGD("%s(%d): can not parse json .....\n", __FUNCTION__, __LINE__);
        return -1;
    }

    str = json_object_get_string(cookie);
    if (strcmp(plat->cookie, str)) {
        memset((void *)plat->cookie, 0, sizeof(plat->cookie));
        memcpy(plat->cookie, str, strlen(str));
    }

    const char *uuid =
        json_object_get_string(json_object_object_get(root, "uuid"));

    mqtt_client_1 = json_object_object_get(
        json_object_object_get(root, "parameterValues"), "Device.MQTT.Client.1");

    if (!mqtt_client_1) {
        HR_LOGD(
            " server response error, can not found Device.MQTT.Client.1 .....\n");
        json_object_put(root);
        return -1;
    }

    const char *broker = json_object_get_string(
        json_object_object_get(mqtt_client_1, "BrokerAddress"));
    int port =
        json_object_get_int(json_object_object_get(mqtt_client_1, "BrokerPort"));
    int alive_time = json_object_get_int(
        json_object_object_get(mqtt_client_1, "KeepAliveTime"));
    const char *X_CU_Topic = json_object_get_string(
        json_object_object_get(mqtt_client_1, "Subscription.1.Topic"));

    const char *username =
        json_object_get_string(json_object_object_get(mqtt_client_1, "Username"));
    const char *password =
        json_object_get_string(json_object_object_get(mqtt_client_1, "Password"));

    HR_LOGD(
        "%s(%d): broker:%s, port:%d, alive time:%d,topic:%s, username:%s, "
        "password:%s, uuid:%s\n",
        __FUNCTION__, __LINE__, broker, port, alive_time, X_CU_Topic,
        username, password, uuid);

    snprintf(plat->host, sizeof(plat->host), "%s", broker);
    plat->port = port;
    plat->alive_time = alive_time;
    snprintf(plat->topic, sizeof(plat->topic), "%s", X_CU_Topic);
    snprintf(plat->username, sizeof(plat->username), "%s", username);
    snprintf(plat->password, sizeof(plat->password), "%s", password);
    snprintf(plat->uuid, sizeof(plat->uuid), "%s", uuid);

    json_object_put(root);

    return 0;
}

static int _uc_dm_action_get_parameter_values(struct uc_platform *plat,
                                              struct json_object *data) {
    int rc = 0;
    char tmp[64] = {0};
    int mid = 0;
    struct json_object *response = NULL, *response_values = NULL;

    HR_LOGD("%s(%d): come in ...........\n", __FUNCTION__, __LINE__);

    if (!data) {
        return -1;
    }

    const char *id = json_object_get_string(json_object_object_get(data, "id"));
    const char *method =
        json_object_get_string(json_object_object_get(data, "method"));

    // parameterNames maybe object or array
    struct json_object *params = json_object_object_get(data, "parameterNames");

    if (!params || !json_object_is_type(params, json_type_array)) {
        HR_LOGE("%s(%d): can not find parameterNames or is not array!\n",
                __FUNCTION__, __LINE__);
        return -1;
    }

    const char *paramters = json_object_to_json_string(
        json_object_object_get(data, "parameterNames"));
    HR_LOGE("%s(%d): id:%s, method:%s, parameters:%s\n", __FUNCTION__, __LINE__,
            id, method, paramters);

    response = json_object_new_object();

    json_object_object_add(response, "cwmpVersion",
                           json_object_new_string("2.0"));

    json_object_object_add(response, "id", json_object_new_string(id));

    response_values = json_object_new_object();
    json_object_object_add(response, "parameterValues", response_values);

    rc = _fill_parameters_with_json_array(params, response_values);
    snprintf(tmp, sizeof(tmp), "%d", rc);
    json_object_object_add(response, "result", json_object_new_string(tmp));

#if DM_DATA_FORMAT_UNION
    _union_parameters(NULL, NULL, response_values, NULL, 0);
#endif

    const char *str = json_object_to_json_string_ext(
        response, JSON_C_TO_STRING_PLAIN | JSON_C_TO_STRING_NOSLASHESCAPE |
                      JSON_C_TO_STRING_PRETTY);
    HR_LOGD("%s(%d): response :%s\n", __FUNCTION__, __LINE__, str);

    mosquitto_publish(plat->mosq, &mid, plat->uuid, strlen(str), str, 0, false);

    json_object_put(response);

    return 0;
}

// 遍历所有层级设置相关属性，如果对象支持 setter 那么会通过该 setter 以 json
// 字符串的形式设置下去 如果 object 不支持 setter
// 那么会继续遍历找到具体叶子节点调用其 setter
static int update_dm_object_using_json_object(struct dm_object *object,
                                              struct json_object *params,
                                              struct hrbuffer *except) {
    json_object_object_foreach(params, key, val) {
        struct dm_value v;

        struct dm_object *dm = dm_object_lookup(key, object);
        if (!dm) {
            HR_LOGD("%s(%d): can not found %s\n", __FUNCTION__, __LINE__, key);
            if (except) {
                if (except->offset != 0) {
                    hrbuffer_append_string(except, ",");
                }

                hrbuffer_append_string(except, key);
            }
            continue;
        }

        memset((void *)&v, 0, sizeof(v));

        // HR_LOGD("%s(%d): type %d...............\n", __FUNCTION__, __LINE__,
        // json_object_get_type(val));
        if (json_object_get_type(val) == json_type_int) {
            dm_value_set_number(&v, json_object_get_int(val));
        } else if (json_object_get_type(val) == json_type_string) {
            dm_value_set_string(&v, json_object_get_string(val));
        } else if (json_object_get_type(val) == json_type_object) {
            // if object accept set operation, we can direct set it, without access
            // childs fields
            if (dm->setter != NULL) {
                dm_value_set_string(&v, json_object_to_json_string_ext(
                                            val, JSON_C_TO_STRING_PLAIN |
                                                     JSON_C_TO_STRING_NOSLASHESCAPE));
            } else {
                update_dm_object_using_json_object(dm, val, except);
            }
        }

        // HR_LOGD("%s(%d): type %d........dm:%s.dm->setter:%p......\n", __FUNCTION__,
        //         __LINE__, json_object_get_type(val), dm->name, dm->setter);
        if (dm->setter) {
            dm->setter(dm, &v);
        }
        dm_value_reset(&v);
    }
    return 0;
}
static int _uc_dm_action_set_parameter_values(struct uc_platform *plat,
                                              struct json_object *data) {
    int rc = 0;
    char tmp[64] = {0};
    int mid = 0;
    struct json_object *response = NULL, *result_string = NULL;
    struct hrbuffer b;

    HR_LOGD("%s(%d): come in ...........\n", __FUNCTION__, __LINE__);
    if (!data) {
        return -1;
    }

    const char *id = json_object_get_string(json_object_object_get(data, "id"));
    const char *method =
        json_object_get_string(json_object_object_get(data, "method"));
    struct json_object *params = json_object_object_get(data, "parameterValues");

    if (!params || !json_object_is_type(params, json_type_object)) {
        HR_LOGE("%s(%d): can not find parameterNames or is not object!\n",
                __FUNCTION__, __LINE__);
        return -1;
    }

    const char *paramters = json_object_to_json_string(
        json_object_object_get(data, "parameterValues"));
    HR_LOGE("%s(%d): id:%s, method:%s, parameters:%s\n", __FUNCTION__, __LINE__,
            id, method, paramters);

    response = json_object_new_object();

    json_object_object_add(response, "cwmpVersion",
                           json_object_new_string("2.0"));

    json_object_object_add(response, "id", json_object_new_string(id));

    hrbuffer_alloc(&b, 512);
    rc = update_dm_object_using_json_object(NULL, params, &b);

    snprintf(tmp, sizeof(tmp), "%d", rc);
    json_object_object_add(response, "result", json_object_new_string(tmp));

    result_string = json_object_new_string(b.data);
    json_object_object_add(response, "resultString", result_string);
    const char *str = json_object_to_json_string_ext(
        response, JSON_C_TO_STRING_PLAIN | JSON_C_TO_STRING_NOSLASHESCAPE |
                      JSON_C_TO_STRING_PRETTY);
    HR_LOGD("%s(%d): response :%s\n", __FUNCTION__, __LINE__, str);

    mosquitto_publish(plat->mosq, &mid, plat->uuid, strlen(str), str, 0, false);

    json_object_put(response);

    hrbuffer_free(&b);

    return 0;
}

// it's only one item in parameterNames array
static int _uc_dm_action_add_instance(struct uc_platform *plat,
                                      struct json_object *data) {
    int rc = 0;
    const char *str = NULL;
    struct dm_value v;
    char tmp[64] = {0};
    struct json_object *response = NULL;
    HR_LOGD("%s(%d): come in ...........\n", __FUNCTION__, __LINE__);
    if (!plat || !data) return -1;

    str = json_object_to_json_string_ext(
        data, JSON_C_TO_STRING_PLAIN | JSON_C_TO_STRING_NOSLASHESCAPE |
                  JSON_C_TO_STRING_PRETTY);

    HR_LOGD("%s(%d): data:%s...........\n", __FUNCTION__, __LINE__, str);

    const char *id = json_object_get_string(json_object_object_get(data, "id"));
    struct json_object *params = json_object_object_get(data, "parameterNames");

    if (!params || !json_object_is_type(params, json_type_array)) {
        HR_LOGE("%s(%d): can not find parameterNames or is not array!\n",
                __FUNCTION__, __LINE__);
        return -1;
    }

    const char *p =
        json_object_get_string(json_object_array_get_idx(params, 0));
    if (!p) {
        rc = 9003;
        goto end;
    }

    HR_LOGD("%s(%d): addInstance %s\n", __FUNCTION__, __LINE__, p);

    struct dm_object *dm = dm_object_lookup(p, NULL);
    if (!dm || !dm->adder) {
        HR_LOGD("%s(%d): can not found %s\n", __FUNCTION__, __LINE__, p);
        rc = 5004;
        goto end;
    }

    memset((void *)&v, 0, sizeof(v));

    dm->adder(dm, &v);

    HR_LOGD("%s(%d): addInstance %s, result:%s\n", __FUNCTION__, __LINE__, p, v.val.string);

end:
    response = json_object_new_object();

    json_object_object_add(response, "cwmpVersion",
                           json_object_new_string("2.0"));

    json_object_object_add(response, "id", json_object_new_string(id));

    snprintf(tmp, sizeof(tmp), "%d", rc);
    json_object_object_add(response, "result", json_object_new_string(tmp));

    if (rc == 0) {
        json_object_object_add(response, "resultString", json_object_new_string(v.val.string));
    } else {
        json_object_object_add(response, "resultString", json_object_new_string(""));
    }
    str = json_object_to_json_string_ext(
        response, JSON_C_TO_STRING_PLAIN | JSON_C_TO_STRING_NOSLASHESCAPE |
                      JSON_C_TO_STRING_PRETTY);
    HR_LOGD("%s(%d): response :%s\n", __FUNCTION__, __LINE__, str);

    mosquitto_publish(plat->mosq, NULL, plat->uuid, strlen(str), str, 0, false);

    dm_value_reset(&v);
    json_object_put(response);

    return 0;
}

// it's only one item in parameterNames array
static int _uc_dm_action_del_instance(struct uc_platform *plat,
                                      struct json_object *data) {
    int rc = 0;
    int length = 0;
    const char *str = NULL;
    char tmp[64] = {0};
    struct json_object *response = NULL;
    HR_LOGD("%s(%d): come in ...........\n", __FUNCTION__, __LINE__);
    if (!plat || !data) return -1;

    str = json_object_to_json_string_ext(
        data, JSON_C_TO_STRING_PLAIN | JSON_C_TO_STRING_NOSLASHESCAPE |
                  JSON_C_TO_STRING_PRETTY);
    HR_LOGD("%s(%d): data:%s...........\n", __FUNCTION__, __LINE__, str);

    const char *id = json_object_get_string(json_object_object_get(data, "id"));
    struct json_object *params = json_object_object_get(data, "parameterNames");

    if (!params || !json_object_is_type(params, json_type_array)) {
        HR_LOGE("%s(%d): can not find parameterNames or is not array!\n",
                __FUNCTION__, __LINE__);
        return -1;
    }

    length = json_object_array_length(params);

    for (int i = 0; i < length; i++) {
        const char *p =
            json_object_get_string(json_object_array_get_idx(params, i));

        HR_LOGD("%s(%d): delInstance %s\n", __FUNCTION__, __LINE__, p);

        struct dm_object *dm = dm_object_lookup(p, NULL);
        if (!dm) {
            HR_LOGD("%s(%d): can not found %s\n", __FUNCTION__, __LINE__, p);
            rc = 5003;
            goto end;
        }

        // we should redirect to it's parent
        dm = dm->parent;
        if (!dm->deleter) {
            HR_LOGD("%s(%d): object %s not support delInstance\n", __FUNCTION__, __LINE__, dm->name);
            rc = 5003;
            goto end;
        }

        struct dm_value v;
        memset((void *)&v, 0, sizeof(v));
        dm_value_set_string_ext(&v, p, 1);
        HR_LOGD("%s(%d): delInstance %s from %s\n", __FUNCTION__, __LINE__, p, dm->parent);
        rc = dm->deleter(dm, &v);
        dm_value_reset(&v);

        HR_LOGD("%s(%d): delInstance %s from %s success\n", __FUNCTION__, __LINE__, p, dm->parent);
    }
end:
    response = json_object_new_object();

    json_object_object_add(response, "cwmpVersion",
                           json_object_new_string("2.0"));

    json_object_object_add(response, "id", json_object_new_string(id));

    snprintf(tmp, sizeof(tmp), "%d", rc);
    json_object_object_add(response, "result", json_object_new_string(tmp));
    str = json_object_to_json_string_ext(
        response, JSON_C_TO_STRING_PLAIN | JSON_C_TO_STRING_NOSLASHESCAPE |
                      JSON_C_TO_STRING_PRETTY);
    HR_LOGD("%s(%d): response :%s\n", __FUNCTION__, __LINE__, str);

    mosquitto_publish(plat->mosq, NULL, plat->uuid, strlen(str), str, 0, false);

    json_object_put(response);

    return 0;
}

static int _uc_dm_action_qos_class_host_op(struct uc_platform *plat,
                                           struct json_object *root) {
    HR_LOGD("%s(%d): come in need implement !...........\n", __FUNCTION__, __LINE__);
    int rc = 0;
    char tmp[64] = {0};

    struct dm_value val;
    const char *query = "_IPC.Qos";
    struct json_object *response = NULL;
    const char *str = NULL;

    const char *id = json_object_get_string(json_object_object_get(root, "id"));

    const char *method =
        json_object_get_string(json_object_object_get(root, "method"));

    const char *data = json_object_to_json_string_ext(json_object_object_get(root, "data"), JSON_C_TO_STRING_PLAIN | JSON_C_TO_STRING_NOSLASHESCAPE);

    if (!method || !data) {
        rc = 5003;
        goto end;
    }

    HR_LOGD("%s(%d): query:%s ...........\n", __FUNCTION__, __LINE__, query);
    struct dm_object *dm = dm_object_lookup(query, NULL);
    if (!dm || !dm->adder || !dm->setter || !dm->deleter) {
        rc = 5002;
        goto end;
    }

    memset((void *)&val, 0, sizeof(val));

    // do not allocate memory again
    dm_value_set_string_ext(&val, data, 1);

    if (!strcmp(method, "addQosClassHost")) {
        rc = dm->adder(dm, &val);
    } else if (!strcmp(method, "setQosClassHost")) {
        rc = dm->setter(dm, &val);
    } else if (!strcmp(method, "delQosClassHost")) {
        rc = dm->deleter(dm, &val);
    }

    dm_value_reset(&val);

end:
    response = json_object_new_object();

    json_object_object_add(response, "cwmpVersion",
                           json_object_new_string("2.0"));

    json_object_object_add(response, "id", json_object_new_string(id));

    snprintf(tmp, sizeof(tmp), "%d", rc);
    json_object_object_add(response, "result", json_object_new_string(tmp));

    if (rc == 0 && !strcmp(method, "addQosClassHost")) {
        // "qosclassID":"Device.QoS.Classification.3"
        // "policerID":"Device.QoS.Policer.6"
    }

    str = json_object_to_json_string_ext(
        response, JSON_C_TO_STRING_PLAIN | JSON_C_TO_STRING_NOSLASHESCAPE |
                      JSON_C_TO_STRING_PRETTY);
    HR_LOGD("%s(%d): loss qosclassID & policerID response :%s\n", __FUNCTION__, __LINE__, str);

    mosquitto_publish(plat->mosq, NULL, plat->uuid, strlen(str), str, 0, false);

    json_object_put(response);
    return 0;
}

static int _uc_dm_action_reboot(struct uc_platform *plat, struct json_object *root) {
    HR_LOGD("%s(%d): come in ...........\n", __FUNCTION__, __LINE__);
    int rc = 0;
    char tmp[64] = {0};

    struct dm_value val;
    const char *query = "Reboot";
    struct json_object *response = NULL;
    const char *str = NULL;

    const char *id = json_object_get_string(json_object_object_get(root, "id"));
    const char *method =
        json_object_get_string(json_object_object_get(root, "method"));

    if (!method) {
        rc = -1;
        goto end;
    }

    if (!strcmp(method, "restoreConfig")) {
        query = "FactoryReset";
    }

    struct dm_object *dm = dm_object_lookup(query, NULL);
    if (!dm || !dm->setter) {
        rc = -1;
        goto end;
    }

    memset((void *)&val, 0, sizeof(val));

    dm_value_set_string_ext(&val, method, 1);

    rc = dm->setter(dm, &val);

    dm_value_reset(&val);

end:
    response = json_object_new_object();

    json_object_object_add(response, "cwmpVersion",
                           json_object_new_string("2.0"));

    json_object_object_add(response, "id", json_object_new_string(id));

    snprintf(tmp, sizeof(tmp), "%d", rc);
    json_object_object_add(response, "result", json_object_new_string(tmp));

    str = json_object_to_json_string_ext(
        response, JSON_C_TO_STRING_PLAIN | JSON_C_TO_STRING_NOSLASHESCAPE |
                      JSON_C_TO_STRING_PRETTY);
    HR_LOGD("%s(%d): response :%s\n", __FUNCTION__, __LINE__, str);

    mosquitto_publish(plat->mosq, NULL, plat->uuid, strlen(str), str, 0, false);

    json_object_put(response);

    return 0;  // rc
}

int uc_platform_create_device_info_sync_message(struct uc_platform *plat,
                                                struct hrbuffer *buf,
                                                const char *param[]) {
    struct json_object *root = NULL, *parameterValues = NULL;
    char tmp[64] = {0};
    root = json_object_new_object();

    json_object_object_add(root, "cwmpVersion", json_object_new_string("2.0"));

    snprintf(tmp, sizeof(tmp), "%d", plat->id);
    plat->id = _inc_id(plat->id);
    json_object_object_add(root, "id", json_object_new_string(tmp));
    json_object_object_add(root, "method", json_object_new_string("devInfoSync"));

    json_object_object_add(root, "cookie", json_object_new_string(plat->cookie));
    json_object_object_add(root, "uuid", json_object_new_string(plat->uuid));

    parameterValues = json_object_new_object();
    json_object_object_add(root, "parameterValues", parameterValues);

    _fill_parameters(param, parameterValues);
    // HR_LOGD("%s(%d): message object %s\n", __FUNCTION__, __LINE__,
    // json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN));

    hrbuffer_append_string(buf, json_object_to_json_string_ext(
                                    root, JSON_C_TO_STRING_PLAIN |
                                              JSON_C_TO_STRING_NOSLASHESCAPE |
                                              JSON_C_TO_STRING_PRETTY));

    json_object_put(root);
    return 0;
}

// E.3.7
static int mqtt_device_info_sync(struct uc_platform *plat) {
    struct hrbuffer b;
    int mid = 0;

    if (!plat)
        return -1;

    hrbuffer_alloc(&b, 512);
    uc_platform_create_device_info_sync_message(
        plat, &b, _mqtt_device_info_sync_parameters);

    HR_LOGD("request message:%s\n", b.data);

    mosquitto_publish(plat->mosq, &mid, plat->uuid, b.offset, b.data, 0, false);
    hrbuffer_free(&b);
    return 0;
}

struct uc_message_response *uc_message_response_new(int id, uc_on_response callback) {
    struct uc_message_response *response = (struct uc_message_response *)calloc(1, sizeof(struct uc_message_response));
    if (!response) {
        return NULL;
    }

    HR_INIT_LIST_HEAD(&response->entry);

    response->id = id;
    response->response = callback;

    hr_list_add_tail(&response->entry, &_message_response_queue);

    return response;
}

static void uc_message_response_free(struct uc_message_response *msg_resp) {
    if (!msg_resp) {
        return;
    }

    // no lock ...
    hr_list_del(&msg_resp->entry);

    HR_INIT_LIST_HEAD(&msg_resp->entry);

    free(msg_resp);
}
static int mtqq_device_bind_active_on_response(struct uc_platform *plat, struct json_object *root) {
    (void)plat;
    (void)root;
    HR_LOGD("%s(%d): come in ...........\n", __FUNCTION__, __LINE__);

    HR_LOGD("%s(%d): come in ........ data:%s\n", __FUNCTION__, __LINE__, json_object_to_json_string(root));
    return 0;
}
// E.3.8
static int mqtt_device_bind_active(struct uc_platform *plat, const char *dev_id, const char *reg_code, const char *app_id) {
    uint32_t id = 1;
    int mid = 0;
    struct json_object *root = NULL, *parameterValues = NULL;
    struct uc_message_response *on_msg_resp = NULL;

    HR_LOGD("%s(%d): come in ...........\n", __FUNCTION__, __LINE__);
    if (!plat || !dev_id || !reg_code || !app_id)
        return -1;

    char tmp[64] = {0};
    root = json_object_new_object();

    json_object_object_add(root, "cwmpVersion", json_object_new_string("2.0"));

    id = plat->id;
    plat->id = _inc_id(plat->id);
    snprintf(tmp, sizeof(tmp), "%u", id);
    json_object_object_add(root, "id", json_object_new_string(tmp));
    json_object_object_add(root, "method", json_object_new_string("bind"));

    json_object_object_add(root, "cookie", json_object_new_string(plat->cookie));
    json_object_object_add(root, "uuid", json_object_new_string(plat->uuid));

    json_object_object_add(root, "devId", json_object_new_string(dev_id));
    json_object_object_add(root, "regCode", json_object_new_string(reg_code));
    json_object_object_add(root, "AppId", json_object_new_string(app_id));

    parameterValues = json_object_new_object();
    json_object_object_add(root, "parameterValues", parameterValues);

    _fill_parameters(_mqtt_device_active_parameters, parameterValues);
    // HR_LOGD("%s(%d): message object %s\n", __FUNCTION__, __LINE__,
    const char *str = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN | JSON_C_TO_STRING_NOSLASHESCAPE | JSON_C_TO_STRING_PRETTY);

    HR_LOGD("%s(%d): message object %s\n", __FUNCTION__, __LINE__, str);

    on_msg_resp = uc_message_response_new(id, mtqq_device_bind_active_on_response);
    if (!on_msg_resp) {
        json_object_put(root);
        return -1;
    }

    mosquitto_publish(plat->mosq, &mid, plat->uuid, strlen(str), str, 0, false);
    json_object_put(root);
    return 0;
}
static int message_response_process_when_needed(struct uc_platform *plat, int id, struct json_object *root) {
    struct uc_message_response *p = NULL, *msg = NULL;

    if (!root) return -1;

    // no lock ...
    hr_list_for_each_entry(p, &_message_response_queue, entry) {
        if (p->id == id) {
            msg = p;
            break;
        }
    }

    if (msg) {
        if (msg->response) {
            msg->response(plat, root);
        }

        uc_message_response_free(msg);

        return 0;
    }

    return -1;
}
void *_mqtt_routin(void *args) {
    (void)args;
    usleep(1000 * 1000 * 3);
    mqtt_device_bind_active((struct uc_platform *)args, "", "123123", "18663792866");

    return NULL;
}

static void _on_log(struct mosquitto *mosq, void *obj, int level,
                    const char *str) {
    (void)mosq;
    (void)obj;
    (void)level;
    HR_LOGD("mqtt %s\n", str);
}

// https://docs.oasis-open.org/mqtt/mqtt/v3.1.1/errata01/os/mqtt-v3.1.1-errata01-os-complete.html#_Table_3.1_-
static void _on_connect(struct mosquitto *mosq, void *obj, int result) {
    struct uc_platform *plat = (struct uc_platform *)obj;

    HR_LOGD("%s(%d): result:%d\n", __FUNCTION__, __LINE__, result);

    if (!result) {
        HR_LOGD("%s(%d): connected, try auto subscribe:%s\n", __FUNCTION__, __LINE__, plat->topic);
        mosquitto_subscribe(mosq, &plat->topic_mid, plat->topic, plat->qos);
    } else {
        if (result) {
            HR_LOGD("Connection error: %s\n", mosquitto_connack_string(result));
        }
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

    struct uc_platform *plat = (struct uc_platform *)obj;

    if (!plat) return;

    HR_LOGD("%s(%d): mid:%d vs %d\n", __FUNCTION__, __LINE__, mid, plat->topic_mid);

    if (plat->topic_mid != -1 && mid == plat->topic_mid) {
        mqtt_device_info_sync(plat);
        // mqtt_device_bind_active(plat, "", "123123", "18663792866");

        plat->topic_mid = -1;
    }

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
    struct json_object *root = NULL;
    struct uc_platform *plat = (struct uc_platform *)obj;
    struct uc_dm_action *act = NULL;

    // if(process_messages == false) return;
    HR_LOGD("%s(%d): receive topic:%s, payloadlen:%d\n", __FUNCTION__, __LINE__,
            message->topic, message->payloadlen);
    if (message->payload) {
        HR_LOGD("%s(%d): receive topic:%s, payloadlen:%d\n%s\n", __FUNCTION__,
                __LINE__, message->topic, message->payloadlen, message->payload);
    }

    if (!message->payload)
        return;

    if (strcmp(plat->topic, message->topic) != 0) {
        //
        HR_LOGE("%s(%d): invalid topic, not our\n", __FUNCTION__, __LINE__);
        return;
    }
    root = json_tokener_parse(message->payload);
    if (!root) {
        HR_LOGE("%s(%d): invalid payload\n", __FUNCTION__, __LINE__);
        return;
    }

    method = json_object_get_string(json_object_object_get(root, "method"));
    if (!method) {
        HR_LOGE("%s(%d): invalid payload\n", __FUNCTION__, __LINE__);
        return;
    }

    id = json_object_get_int(json_object_object_get(root, "id"));

    HR_LOGE("%s(%d): id:%d\n", __FUNCTION__, __LINE__, id);
    // process message queue if possible
    if (0 == message_response_process_when_needed(plat, id, root)) {
        printf("id:%d have been processed!\n", id);
        json_object_put(root);
        return;
    }

    for (act = &_uc_dm_action[0]; act != NULL; act++) {
        if (!strcmp(act->name, method)) {
            break;
        }
    }

    if (act && act->action) {
        act->action(plat, root);
    } else {
        HR_LOGE("%s(%d): current method :%s not support\n", __FUNCTION__, __LINE__,
                method);
    }

    json_object_put(root);
}
static void _signal_action(int signum, siginfo_t *siginfo, void *sigcontext) {
    (void)sigcontext;

    HR_LOGD("%s(%d): ........signum:%d\n", __FUNCTION__, __LINE__, signum);

    // if (SIGUSR1 == signum) {
    mosquitto_disconnect(_platform.mosq);
    mosquitto_loop_stop(_platform.mosq, 1);
    //}
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    int rc = 0;
    struct sigaction action;
    memset(&action, 0, sizeof(action));
    sigemptyset(&action.sa_mask);
    action.sa_sigaction = _signal_action;
    sigaction(SIGUSR1, &action, NULL);

    memset((void *)&_platform, 0, sizeof(_platform));

    _platform.id = 1;
    _platform.qos = 0;

    _platform.req = url_request_new();

    dm_Device_init(NULL);
    dm_Reboot_init(NULL);
    dm_FactoryReset_init(NULL);

    struct dm_object *Device = dm_object_lookup("Device", NULL);

    struct dm_object *object = dm_object_lookup("Device.X_CU_LockEnable", NULL);
    if (object) {
        struct dm_value val;
        memset((void *)&val, 0, sizeof(val));
        object->getter(object, &val);
        HR_LOGD("lock:%d\n", val.val.number);
        val.val.number = !val.val.number;
        HR_LOGD("adjust lock:%d\n", val.val.number);
        object->setter(object, &val);
        dm_value_reset(&val);
    }

    object = dm_object_lookup("X_CU_LockEnable", Device);
    if (object) {
        struct dm_value val;
        memset((void *)&val, 0, sizeof(val));
        object->getter(object, &val);
        dm_value_reset(&val);
        HR_LOGD("direct query from Device lock:%d\n", val.val.number);
    }

    object = dm_object_lookup("Device.DeviceInfo.SerialNumber", NULL);
    if (object) {
        struct dm_value val;
        memset((void *)&val, 0, sizeof(val));
        object->getter(object, &val);
        HR_LOGD("serial:%s\n", val.val.string);
        dm_value_reset(&val);
    }

    object = dm_object_lookup("Device.DeviceInfo.ModelName", NULL);
    if (object) {
        struct dm_value val;
        memset((void *)&val, 0, sizeof(val));
        object->getter(object, &val);
        HR_LOGD("model 1 :%s\n", val.val.string);
        dm_value_reset(&val);
    }

    dm_object_new_ext("Device.DeviceInfo.ModelName2", DM_TYPE_STRING,
                      NULL, NULL);

    object = dm_object_lookup("Device.DeviceInfo.ModelName2", NULL);
    if (object) {
        struct dm_value val;
        memset((void *)&val, 0, sizeof(val));
        object->getter(object, &val);
        HR_LOGD("model 2:%s\n", val.val.string);
        dm_value_reset(&val);
    }

    object = dm_object_lookup("Device.DeviceInfo.", NULL);
    if (object) {
        struct dm_value val;
        memset((void *)&val, 0, sizeof(val));
        object->getter(object, &val);
        HR_LOGD("Device.DeviceInfo:%s\n", val.val.string);
        dm_value_reset(&val);
    }

    object = dm_object_lookup("Device.", NULL);
    if (object) {
        struct dm_value val;
        memset((void *)&val, 0, sizeof(val));
        object->getter(object, &val);
        HR_LOGD("Device.:%s\n", val.val.string);
        dm_value_reset(&val);
    }

    uc_platform_stage_1_devauth(&_platform);
    uc_platform_stage_2_getmqttserver(&_platform);

    // release socket
    url_request_free(_platform.req);
    _platform.req = NULL;

    object = dm_object_lookup("Device.DeviceInfo.X_CU_CUEI", NULL);
    if (object) {
        struct dm_value val;
        memset((void *)&val, 0, sizeof(val));
        object->getter(object, &val);
        // using cuei as mqtt's client id
        snprintf(_platform.client_id, sizeof(_platform.client_id), "%s",
                 val.val.string);
        dm_value_reset(&val);
    } else {
        HR_LOGD("%s(%d): .........can not got cuei as client id..........\n", __FUNCTION__, __LINE__);
        exit(-1);
    }

#if 0
    // clang-format off
    const char* str = "{\"Device.WiFi.X_CU_ACL\":{\"2G\":{\"OperatingFrequencyBand\":\"2.4GHz\",\"MACAddressControlEnabled\":true,\"MacFilterPolicy\":0,\"WMacFilters\":{\"HostName\":\"M2006J10C\",\"MACAddress\":\"34:1C:F0:43:49:23\"},\"BMacFilters\":{\"HostName\":\"M2006J10C\",\"MACAddress\":\"34:1C:F0:43:49:23\"}},\"5G\":{\"OperatingFrequencyBand\":\"5GHz\",\"MACAddressControlEnabled\":true,\"MacFilterPolicy\":0,\"WMacFilters\":{\"HostName\":\"M2006J10C\",\"MACAddress\":\"34:1C:F0:43:49:23\"},\"BMacFilters\":{\"HostName\":\"M2006J10C\",\"MACAddress\":\"34:1C:F0:43:49:23\"}}}}";
    // clang-format off

    struct json_object *r = json_tokener_parse(str);

    update_dm_object_using_json_object(NULL, r, NULL);
    json_object_put(r);
#endif

    HR_LOGD("%s(%d): try connect mqtt :%s:%d\n", __FUNCTION__, __LINE__,
            _platform.host, _platform.port);
    mosquitto_lib_init();

    HR_LOGD("client id:%s\n", _platform.client_id);
    _platform.mosq = mosquitto_new(_platform.client_id, false, &_platform);
    if (!_platform.mosq) {

        HR_LOGD("%s(%d): error can not instance mosquitto\n", __FUNCTION__,
                __LINE__);
        dm_object_free(NULL);
        return -1;
    }

    mosquitto_log_callback_set(_platform.mosq, _on_log);

    mosquitto_subscribe_callback_set(_platform.mosq, _on_subscribe);
    mosquitto_connect_callback_set(_platform.mosq, _on_connect);
    // mosquitto_connect_with_flags_callback_set(_platform.mosq,
    // _on_connect_with_flags);
    mosquitto_disconnect_callback_set(_platform.mosq, _on_disconnect);
    mosquitto_message_callback_set(_platform.mosq, _on_message);

    mosquitto_tls_opts_set(_platform.mosq, 0 /*SSL_VERIFY_NONE*/, NULL, NULL);

    const char *cafile = "/home/alex/workspace/workspace/libuv/mqtt_cacert.pem";

    mosquitto_tls_set(_platform.mosq, cafile, NULL, NULL, NULL, NULL);
    mosquitto_tls_insecure_set(_platform.mosq, 0);
    mosquitto_tls_opts_set(_platform.mosq, 0, NULL, NULL);

    do {
        HR_LOGD("%s(%d): connect:%s:%d\n", __FUNCTION__, __LINE__, _platform.host, _platform.port);
        rc = mosquitto_connect_bind(_platform.mosq, _platform.host, _platform.port,
                           _platform.alive_time, NULL);
    } while (rc != MOSQ_ERR_SUCCESS);

    int result = pthread_create(&_platform.tid, NULL, _mqtt_routin, &_platform);
    if (result != 0) {
        // return -1;
    }

    mosquitto_loop_forever(_platform.mosq, -1, 1);

    mosquitto_destroy(_platform.mosq);
    _platform.mosq = NULL;

    mosquitto_lib_cleanup();
    dm_object_free(NULL);
    
    pthread_cancel(_platform.tid);
    pthread_join(_platform.tid, NULL);

    {
        struct uc_message_response *n, *p;
        hr_list_for_each_entry_safe(p, n, &_message_response_queue, entry) {
            uc_message_response_free(p);
        }
    }
    return 0;
}
