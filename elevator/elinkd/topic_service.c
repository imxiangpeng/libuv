
// mxp, 20250710, implement iot service topic

#include "topic_service.h"

#include <cjson/cJSON.h>
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "hr_log.h"
#include "iot.h"
#include "service.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define SVC_METHOD_PREFIX "thing.service."

static char _stored_id[64] = {0};

static int _on_svc_message(void* payload, int len) {
    char* id = NULL;
    char* method = NULL;
    struct svc_action* act = NULL;
    cJSON *root = NULL, *params = NULL;
    int skip_length = 0;

    if (!payload || len == 0) {
        HR_LOGE("%s(%d): invalid method ...\n", __FUNCTION__, __LINE__);
        return -1;
    }

    HR_LOGD("%s(%d): payload:%s\n", __FUNCTION__, __LINE__, (const char*)payload);
    root = cJSON_ParseWithLength((const char*)payload, len);
    if (!root) {
        return -1;
    }

    id = cJSON_GetStringValue(cJSON_GetObjectItem(root, "id"));
    if (!id) {
        cJSON_Delete(root);
        return -1;
    }

    snprintf(_stored_id, sizeof(_stored_id), "%s", id);

    method = cJSON_GetStringValue(cJSON_GetObjectItem(root, "method"));
    if (!method) {
        cJSON_Delete(root);
        return -1;
    }

    skip_length = strlen(SVC_METHOD_PREFIX);
    if (0 != strncmp(method, SVC_METHOD_PREFIX, skip_length)) {
        cJSON_Delete(root);
        return -1;
    }

    method += skip_length;

    params = cJSON_GetObjectItem(root, "params");
    if (!params) {
        cJSON_Delete(root);
        return -1;
    }

    for (act = &svc_action_tbl[0]; act != NULL; act++) {
        // thing.service.StartAutoFloorCalibration
        if (!strcmp(act->name, method)) {
            /*int rc =*/act->method(params);
            break;
        }
    }

    cJSON_Delete(root);

    return 0;
}

int topic_service_init(const char* public_key, const char* device_name) {
    if (!public_key || !device_name) {
        return -1;
    }

    struct topic* t = (struct topic*)calloc(1, sizeof(struct topic) * svc_action_tbl_size);
    if (!t) {
        return -1;
    }

    for (size_t i = 0; i < svc_action_tbl_size; i++) {
        if (!svc_action_tbl[i].name) {
            continue;
        }
        snprintf(t[i].name, sizeof(t[i].name), "service/%s", svc_action_tbl[i].name);
        snprintf(t[i].topic, sizeof(t[i].topic), "/sys/%s/%s/thing/%s", public_key, device_name, t[i].name);

        t[i].type = TOPIC_TYPE_SUBSCRIBE,
        t[i].callback.on_message = _on_svc_message,
        iot_topic_register(&t[i]);
    }

    return 0;
}
