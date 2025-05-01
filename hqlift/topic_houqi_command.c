
#include <cjson/cJSON.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "elevator.h"
#include "iot_topic.h"

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

// static int _on_upload_record_list_publish(void** payload, int* len) {
//     return 0;
// }
struct uviot_topic topic_command = {
    .name = "Command",
    .topic = {0},
    .type = TOPIC_TYPE_SUBSCRIBE,
    .callback.on_message = _on_command_message,
};

// struct iot_topic topic_upload_record_list = {
//     .name = "uploadRecordList",
//     .topic = {0},
//     .type = TOPIC_TYPE_PUBLISH,
//     .callback.on_publish = _on_upload_record_list_publish,
// };

int topic_houqi_command_init(struct uviot* iot, const char* public_key, const char* device_name) {
    (void)public_key;
    (void)device_name;
    const char* serialno = elevator_deviceid();
    snprintf(topic_command.topic, sizeof(topic_command.topic), "/API/V1/Down/%s/Command", serialno);
    uviot_topic_register(iot, &topic_command);
    return 0;
}
