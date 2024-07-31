/*
 * Copyright (C) 2024 Inspur Group Co., Ltd. Unpublished
 *
 * Inspur Group Co., Ltd.
 * Proprietary & Confidential
 *
 * This source code and the algorithms implemented therein constitute
 * confidential information and may comprise trade secrets of Inspur
 * or its associates, and any use thereof is subject to the terms and
 * conditions of the Non-Disclosure Agreement pursuant to which this
 * source code was originally received.
 */

#include <cjson/cJSON.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "dm_object.h"
#include "hr_log.h"

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

#define CU_MQTT_SERVER_CONF "cu_mqtt_server.json"
// #define CU_DATA_DIR "/data/etc/cu/"
#define CU_ETC_DIR "/etc/cu/"

// you must free the pointer
static size_t _read_file(const char* path, char** buf) {
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

    data = (char*)malloc(sb.st_size);
    if (!data) {
        close(fd);
        return -1;
    }
    memset((void*)data, 0, sb.st_size);

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

static int MQTT_Client_getter(struct dm_object* self, struct dm_value* val) {
    (void)self;
    (void)val;
    cJSON* root = NULL;

    size_t len = 0;
    char* data = NULL;

    len = _read_file(CU_ETC_DIR CU_MQTT_SERVER_CONF, &data);
    if (data) {
        root = cJSON_ParseWithLength(data, len);
        free(data);
    } else {
        root = cJSON_CreateObject();
    }

    if (!root) return -1;

    if (!cJSON_GetObjectItem(root, "BrokerAddress")) {
        cJSON_AddItemToObject(root, "BrokerAddress", cJSON_CreateString("rtm.ossapp.chinaunicom.cn"));
    }
    if (!cJSON_GetObjectItem(root, "BrokerPort")) {
        cJSON_AddItemToObject(root, "BrokerPort", cJSON_CreateNumber(1890));
    }

    if (!cJSON_GetObjectItem(root, "Username")) {
        cJSON_AddItemToObject(root, "Username", cJSON_CreateString(""));
    }

    if (!cJSON_GetObjectItem(root, "Password")) {
        cJSON_AddItemToObject(root, "Password", cJSON_CreateString(""));
    }
    if (!cJSON_GetObjectItem(root, "KeepAliveTime")) {
        cJSON_AddItemToObject(root, "KeepAliveTime", cJSON_CreateNumber(60));
    }

    char* str = cJSON_PrintUnformatted(root);
    dm_value_set_string(val, str);
    free(str);

    cJSON_Delete(root);

    return 0;
}

static int MQTT_Client_setter(struct dm_object* self, struct dm_value* val) {
    (void)self;
    (void)val;

    HR_LOGD("%s(%d): do nothing, we use mqtt information when auth\n", __FUNCTION__, __LINE__);

    return 0;
}

static int dm_Device_MQTT_Client_i_init(int id, struct dm_object* parent) {
    char name[32] = {0};
    snprintf(name, sizeof(name), "%d", id);
    dm_object_new(name, DM_TYPE_OBJECT, MQTT_Client_getter, MQTT_Client_setter, parent);
    return 0;
}

int dm_Device_MQTT_Client_init(struct dm_object* parent) {
    struct dm_object* MQTT_Client = dm_object_new("MQTT.Client", DM_TYPE_OBJECT, dm_object_attribute, NULL, parent);
    dm_Device_MQTT_Client_i_init(1, MQTT_Client);
    return 0;
}
