
// mxp, 20250502, implement houqi topic: /API/V1/Down/%s/Command

#include <cjson/cJSON.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "elevator.h"
#include "uviot.h"

extern void topic_houqi_liftstate_post(void);

static int _on_command_message(void* payload, int len) {
    char* type = NULL;
    cJSON* root = NULL;
    if (!payload || len == 0) {
        return -1;
    }

    root = cJSON_ParseWithLength((const char*)payload, len);
    if (!root) {
        return -1;
    }

    printf("command message %d -> %s\n", len, (char*)payload);

    type = cJSON_GetStringValue(cJSON_GetObjectItem(root, "type"));
    if (!type) {
        cJSON_Delete(root);
        return -1;
    }

    if (0 == strcmp("Sendstate", type)) {
        topic_houqi_liftstate_post();
        cJSON_Delete(root);
        return 0;
    }

    if (0 == strcmp("Sendvideo", type)) {
        // todo
        cJSON_Delete(root);

        // ipc-property set /ipc/livertmp/location rtmp://srs.hqszjs.com:1935/live/LC40025120000001
        // ipc-property set /ipc/livertmp/enabled true

        char cmd[512] = {0};
        snprintf(cmd, sizeof(cmd), "ipc-property set /ipc/livertmp/location rtmp://srs.hqszjs.com:1935/live/%s;ipc-property set /ipc/livertmp/enabled true", elevator_serialno());
        system(cmd);

        return 0;
    }

    if (0 == strcmp("videoPlayBack", type)) {
        char *start_time = NULL, *end_time = NULL;
        int file_index = -1;
        double val = cJSON_GetNumberValue(cJSON_GetObjectItem(root, "fileIndex"));
        if (!isnan(val)) {
            file_index = (int)val;
            (void)file_index;
        }

        // 2024-04-06 15:57:20
        start_time = cJSON_GetStringValue(cJSON_GetObjectItem(root, "startTime"));
        end_time = cJSON_GetStringValue(cJSON_GetObjectItem(root, "endTime"));

        if (!start_time || !end_time) {
        }
        // todo
        cJSON_Delete(root);
        return 0;
    }

    if (0 == strcmp("videoPlayBack", type)) {
        // todo
        cJSON_Delete(root);
        return 0;
    }

    if (0 == strcmp("uploadRecord", type)) {
        // todo
        cJSON_Delete(root);
        return 0;
    }
    if (0 == strcmp("recordDownload", type)) {
        // todo
        cJSON_Delete(root);
        return 0;
    }

    return 0;
}

static int _on_command_response_publish(void** payload, int* len) {
    (void)payload;
    (void)len;

    return 0;
}
struct uviot_topic topic_command = {
    .name = "Command",
    .topic = {0},
    .type = TOPIC_TYPE_SUBSCRIBE,
    .callback.on_message = _on_command_message,
};

struct uviot_topic topic_command_response = {
    .name = "Command/Response",
    .topic = {0},
    .type = TOPIC_TYPE_PUBLISH,
    .callback.on_publish = _on_command_response_publish,
};

// /API/V1/Down/序列号/Command/Response

int topic_houqi_command_init(struct uviot* iot, const char* public_key, const char* device_name) {
    (void)public_key;
    (void)device_name;
    const char* serialno = elevator_serialno();  //"244200000E480001"; //elevator_deviceid();
    snprintf(topic_command.topic, sizeof(topic_command.topic), "/API/V1/Down/%s/Command", serialno);
    uviot_topic_register(iot, &topic_command);

    snprintf(topic_command_response.topic, sizeof(topic_command_response.topic), "/API/V1/Down/%s/Command/Response", serialno);
    uviot_topic_register(iot, &topic_command_response);

    return 0;
}

