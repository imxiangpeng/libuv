#include <string.h>

#include "dm_Device.h"
#include "dm_object.h"
#include "hr_buffer.h"
#include "hr_log.h"
#include "json-c/json.h"

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
};

const char* _stage_1_parameter[] = {
    "Device.DeviceInfo.ModelName",
    "Device.DeviceInfo.SerialNumber",
    "Device.DeviceInfo.SoftwareVersion",
    "Device.DeviceInfo.X_CU_FriendlyName",
    "Device.X_CU_WAN.ConnectionType",
    "Device.DeviceInfo.X_CU_CUEI",
    "Device.X_CU_WAN.MAC",
    NULL};

int uc_platform_create_request_message(struct hrbuffer* buf, const char* method, const char* param[]) {
    struct json_object *root = NULL, *result = NULL, *parameterValues = NULL;
    root = json_object_new_object();

    json_object_object_add(root, "cwmpVersion", json_object_new_string("1.0"));
    json_object_object_add(root, "id", json_object_new_string("53"));

    // json_object_object_add(root, "cookie", json_object_new_string(cookie));
    if (method)
        json_object_object_add(root, "method", json_object_new_string(method));
    json_object_object_add(root, "accType", json_object_new_string("router"));

    parameterValues = json_object_new_object();
    json_object_object_add(root, "parameterValues", parameterValues);

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

    HR_LOGD("%s(%d): message object %s\n", __FUNCTION__, __LINE__, json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN));

    hrbuffer_append_string(buf, json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN));

    json_object_put(root);
    return 0;
}
// E.3.1
int uc_platform_init_stage_1(struct uc_platform* plat) {
    struct hrbuffer b;

    if (!plat) return -1;

    hrbuffer_alloc(&b, 512);
    uc_platform_create_request_message(&b, "devAuth", _stage_1_parameter);
    return 0;
}

int main(int argc, char** argv) {
    struct hrbuffer b;

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

    hrbuffer_reset(&b);
    uc_platform_create_request_message(&b, "devAuth", _stage_1_parameter);
    // destroy

    return 0;
}