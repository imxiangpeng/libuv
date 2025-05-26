// mxp, 20250502, implement houqi topic: /API/V1/Down/%s/Command

#define _GNU_SOURCE
#define _XOPEN_SOURCE 600
#include <cjson/cJSON.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "elevator.h"
#include "uviot.h"

#define IPC_MEDIA_RECORD_DIR "/media/mmcblk0p1"

        // 2024-04-06 15:57:20
#define COMMAND_DATE_STRING_FORMAT "%Y-%m-%d %H:%M:%S"
static struct uviot* _iot = NULL;
extern void topic_houqi_liftstate_post(void);

static int publish_upload_record_response();

static uint64_t command_date_format_string_to_seconds(const char* date) {
    struct tm tm;
    // must reset tm, because strptime not fill all fields
    memset((void*)&tm, 0, sizeof(tm));
    if (strptime(date, COMMAND_DATE_STRING_FORMAT, &tm) == NULL) {
       return 0; 
    }
    
    return mktime(&tm);
}
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

    // same rtmp url with Sendvideo
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

    //
    if (0 == strcmp("uploadRecord", type)) {
        uint64_t timestamp_begin= 0, timestamp_end = 0;
        char *start_time = NULL, *end_time = NULL;
        // 2024-04-06 15:57:20
        start_time = cJSON_GetStringValue(cJSON_GetObjectItem(root, "startTime"));
        end_time = cJSON_GetStringValue(cJSON_GetObjectItem(root, "endTime"));

        if (!start_time || !end_time) {
            cJSON_Delete(root);
            return -1;
        }
        
        timestamp_begin = command_date_format_string_to_seconds(start_time);
        timestamp_end = command_date_format_string_to_seconds(end_time);
        if (timestamp_begin == 0 || timestamp_end == 0) {
            cJSON_Delete(root);
            return -1;
        }

        // we should review ipc media record dir and filter record files
        
        // todo
        cJSON_Delete(root);
        return 0;
    }

    //
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

// {
//    "type":"uploadRecordList",
//    "elevatorNo":"GD500103000086",
//    "macAddr":"LC4005CB4E213B8C9",
//    "RecordBean":[
//        {
//            fileIndex: 文件索引号
//            fileName: 文件名
//        }
//    ]
//}

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

    _iot = iot;
    const char* serialno = elevator_serialno();  //"244200000E480001"; //elevator_deviceid();
    snprintf(topic_command.topic, sizeof(topic_command.topic), "/API/V1/Down/%s/Command", serialno);
    uviot_topic_register(iot, &topic_command);

    snprintf(topic_command_response.topic, sizeof(topic_command_response.topic), "/API/V1/Down/%s/Command/Response", serialno);
    uviot_topic_register(iot, &topic_command_response);

    return 0;
}

static int publish_upload_record_response() {
    return uviot_publish_async(_iot, &topic_command_response);
}
