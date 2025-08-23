
// mxp, 20250710, implement iot property topic

#include "topic_property.h"

#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cjson/cJSON.h"
#include "property.h"
#include "topic.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#ifndef HR_LOGD
#define HR_LOGD printf
#endif

#ifndef HR_LOGE
#define HR_LOGE printf
#endif

enum {
    PROPERTY_TOPIC_POST = 0,
    PROPERTY_TOPIC_SET,
    PROPERTY_TOPIC_GET,
    _PROPERTY_TOPIC_MAX,
};

static int _on_property_publish(void** payload, int* len) {
    char tmp[256] = {0};
    cJSON *root = NULL, *param = NULL;

    root = cJSON_CreateObject();
    if (!root)
        return -1;

    // test code, please only response when needed
    snprintf(tmp, sizeof(tmp), "%d", topic_generate_mid());
    cJSON_AddStringToObject(root, "id", tmp);
    cJSON_AddStringToObject(root, "version", "1.0.0");

    param = cJSON_AddObjectToObject(root, "params");

    for (size_t i = 0; i < __PROPERTY_MAX; i++) {
        struct property* prop = &properties_tbl[i];
        if (!prop->name || prop->dirty == 0 || prop->type == E_UNKNOWN) {
            continue;
        }

        prop->dirty = 0;
        // only call get when it's not null
        // we can directly access value when it's null
        if (prop->getter) {
            if (prop->getter(prop) != 0) {
                continue;
            }
        }

        // verify value's type
        switch (prop->value.type) {
            case E_NUMBER:
                cJSON_AddNumberToObject(param, prop->name, prop->value.val.number);
                break;
            case E_DECIMAL:
                cJSON_AddNumberToObject(param, prop->name, prop->value.val.decimal);
                break;
            case E_STRING: {
                if (prop->value.val.string) {
                    cJSON_AddStringToObject(param, prop->name, prop->value.val.string);
                    break;
                } else {
                    cJSON_AddStringToObject(param, prop->name, "");
                }
                break;
            }
            case E_BOOLEAN:
                cJSON_AddBoolToObject(param, prop->name, prop->value.val.boolean);
                break;
            default:
                // invalid value, drop it
                break;
        }

        // do not free it
        // property_value_reset(&prop->value);
    }

    if (cJSON_GetArraySize(param) == 0) {
        cJSON_Delete(root);
        return -1;
    }

    *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!*payload)
        return -1;

    *len = strlen(*payload);
    HR_LOGD("publish: %s\n", (char*)*payload);

    return 0;
}
static int _on_property_set(void* payload, int len) {
    double number = 0;
    const char* string = NULL;
    struct property_value value;
    char* method = NULL;
    // double val = 0;
    // const char* val_str = NULL;
    cJSON *root = NULL, *params = NULL, *ele = NULL;
    if (!payload || len == 0) {
        HR_LOGE("%s(%d): invalid method ...\n", __FUNCTION__, __LINE__);
        return -1;
    }

    HR_LOGD("set %d -> %s\n", len, (char*)payload);

    root = cJSON_ParseWithLength((const char*)payload, len);
    if (!root) {
        return -1;
    }

    method = cJSON_GetStringValue(cJSON_GetObjectItem(root, "method"));
    if (!method || 0 != strcmp("thing.service.property.set", method)) {
        cJSON_Delete(root);
        return -1;
    }

    params = cJSON_GetObjectItem(root, "params");
    if (!params || !cJSON_IsObject(params)) {
        cJSON_Delete(root);
        return -1;
    }

    memset((void*)&value, 0, sizeof(value));
    cJSON_ArrayForEach(ele, params) {
        // HR_LOGD("%s(%d):ele: %s -> type:%d\n", __FUNCTION__, __LINE__, ele->string, ele->type);

        if (!ele->string) continue;

        struct property* prop = property_get(ele->string);
        if (!prop || !prop->setter) {
            HR_LOGD("not support property:%s\n", ele->string);
            continue;
        }

        // verify property's type, value type maybe not set
        switch (prop->type) {
            case E_NUMBER:
                if (cJSON_IsNumber(ele)) {
                    number = cJSON_GetNumberValue(ele);
                    if (!isnan(number)) {
                        property_value_set_number(&value, (int64_t)number);
                        prop->setter(prop, &value);
                        property_value_reset(&value);
                    }
                } else if (cJSON_IsBool(ele)) {
                    // support convert boolean to number
                    int boolean = cJSON_IsTrue(ele) ? 1 : 0;
                    property_value_set_number(&value, boolean);
                    prop->setter(prop, &value);
                    property_value_reset(&value);
                } else {
                   // not support
                }
                break;
            case E_DECIMAL:
                number = cJSON_GetNumberValue(ele);
                if (!isnan(number)) {
                    property_value_set_decimal(&value, number);
                    prop->setter(prop, &value);
                    property_value_reset(&value);
                }

                break;
            case E_STRING: {
                string = cJSON_GetStringValue(ele);
                printf("set string :%s, %ld\n", string, strlen(string));
                if (string) {
                    // string is const, no need free
                    property_value_set_string_ext(&value, string, 1);
                    prop->setter(prop, &value);
                    property_value_reset(&value);
                    break;
                }
                break;
            }
            case E_BOOLEAN:
                if (cJSON_IsBool(ele)) {
                    int boolean = cJSON_IsTrue(ele) ? 1 : 0;
                    property_value_set_boolean(&value, boolean);
                    prop->setter(prop, &value);
                    property_value_reset(&value);
                }
                break;

            default:
                // invalid value, drop it
                break;
        }
    }

    cJSON_Delete(root);

    // auto report dirty properties
    topic_property_report();

    return 0;
}

// only mark property as dirty
static int _on_property_get(void* payload, int len) {
    char* method = NULL;
    // double val = 0;
    // const char* val_str = NULL;
    cJSON *root = NULL, *params = NULL, *ele = NULL;
    if (!payload || len == 0) {
        HR_LOGE("%s(%d): invalid method ...\n", __FUNCTION__, __LINE__);
        return -1;
    }

    HR_LOGD("get %d -> %s\n", len, (char*)payload);

    root = cJSON_ParseWithLength((const char*)payload, len);
    if (!root) {
        return -1;
    }

    method = cJSON_GetStringValue(cJSON_GetObjectItem(root, "method"));
    if (!method || 0 != strcmp("thing.service.property.get", method)) {
        cJSON_Delete(root);
        return -1;
    }

    params = cJSON_GetObjectItem(root, "params");
    if (!params) {
        cJSON_Delete(root);
        return -1;
    }

    cJSON_ArrayForEach(ele, params) {
        const char* name = NULL;

        // support two methods:
        // 1.  ["eguard_door_close_timeout","eguard_door_repeat_timeout","sw_version"]
        // 2.  {"eguard_door_close_timeout":{},"eguard_door_repeat_timeout":{},"sw_version":{}}
        if (cJSON_IsString(ele)) {
            name = cJSON_GetStringValue(ele);
        } else if (cJSON_IsObject(ele)) {
            name = ele->string;
        }

        if (!name) {
            continue;
        }

        struct property* prop = property_get(name);
        if (!prop) {
            HR_LOGD("not support property:%s\n", name);
            continue;
        }
        prop->dirty = 1;
    }

    // schedule report
    topic_property_report();

    cJSON_Delete(root);
    return 0;
}

static struct topic _iot_property_topics[_PROPERTY_TOPIC_MAX] = {
    [PROPERTY_TOPIC_POST] = {
        .name = "event/property/post",
        .topic = {0},
        .period = 0,
        .auto_publish = 1,
        .type = TOPIC_TYPE_PUBLISH,
        .callback.on_publish = _on_property_publish,
    },
    [PROPERTY_TOPIC_SET] = {
        .name = "service/property/set",
        .topic = {0},
        .type = TOPIC_TYPE_SUBSCRIBE,
        .callback.on_message = _on_property_set,
    },
    [PROPERTY_TOPIC_GET] = {
        .name = "service/property/get",
        .topic = {0},
        .type = TOPIC_TYPE_SUBSCRIBE,
        .callback.on_message = _on_property_get,
    },
};

int topic_property_init(const char* public_key, const char* device_name) {
    (void)public_key;
    (void)device_name;

    for (size_t i = 0; i < ARRAY_SIZE(_iot_property_topics); i++) {
        struct topic* t = &_iot_property_topics[i];
        snprintf(t->topic, sizeof(t->topic), "/sys/%s/%s/thing/%s", public_key, device_name, t->name);
        iot_topic_register(t);
    }

    return 0;
}

void topic_property_report(void) {
    iot_topic_publish_async(&_iot_property_topics[PROPERTY_TOPIC_POST]);
}
