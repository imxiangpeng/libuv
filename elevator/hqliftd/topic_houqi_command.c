// mxp, 20250502, implement houqi topic: /API/V1/Down/%s/Command

#define _GNU_SOURCE
#define _XOPEN_SOURCE 600

#include <cjson/cJSON.h>
#include <dirent.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include "elevator.h"
#include "hr_buffer.h"
#include "hr_log.h"
#include "uviot.h"

// #define IPC_MEDIA_RECORD_DIR "/media/mmcblk0p1"
#define IPC_MEDIA_RECORD_DIR "./media"
#define MEDIA_RECORD_DURATION 300  // 5min

// 2024-04-06 15:57:20
#define COMMAND_DATE_STRING_FORMAT "%Y-%m-%d %H:%M:%S"
// 2025-05-26_18-22-00.mp4
#define MEDIA_RECORD_DATE_STRING_FORMAT "%Y-%m-%d_%H-%M-%S"

static struct uviot* _iot = NULL;

struct record {
    uint64_t timestamp;
    char name[128];
};

struct hrbuffer _record_lists;

extern void topic_houqi_liftstate_post(void);

static int publish_upload_record_response();
static int traverse_media_record_list(uint64_t begin, uint64_t end);

static uint64_t command_date_format_string_to_seconds(const char* date) {
    struct tm tm;
    // must reset tm, because strptime not fill all fields
    memset((void*)&tm, 0, sizeof(tm));
    if (strptime(date, COMMAND_DATE_STRING_FORMAT, &tm) == NULL) {
        return 0;
    }

    return mktime(&tm);
}

static uint64_t media_record_date_format_string_to_seconds(const char* date) {
    struct tm tm;
    // must reset tm, because strptime not fill all fields
    memset((void*)&tm, 0, sizeof(tm));
    if (strptime(date, MEDIA_RECORD_DATE_STRING_FORMAT, &tm) == NULL) {
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
        uint64_t timestamp_begin = 0, timestamp_end = 0;
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

        if (!_record_lists.data) {
            hrbuffer_alloc(&_record_lists, sizeof(struct record) * 50);
        }
        hrbuffer_reset(&_record_lists);
        // we should review ipc media record dir and filter record files
        if (traverse_media_record_list(timestamp_begin, timestamp_end) > 0) {
            publish_upload_record_response();
        }
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

    // empty
    if (!_record_lists.data || _record_lists.offset == 0) {
        return -1;
    }

    if (_record_lists.offset % sizeof(struct record) != 0) {
        HR_LOGE("%s(%d): data maybe invalid ...\n");
    }
    struct record* r = (struct record*)_record_lists.data;

    size_t count = _record_lists.offset / sizeof(struct record);

    for (size_t i = 0; i < count; i++) {
        printf("%ld -> %ld : %s\n", i, r[i].timestamp, r[i].name);
    }

    cJSON* root = cJSON_CreateObject();
    if (!root)
        return -1;

    cJSON_AddStringToObject(root, "type", "uploadRecordList");
    // houqi's macAddr is serialno, length must > 12
    cJSON_AddStringToObject(root, "macAddr", elevator_serialno());  // elevator_mac
    cJSON_AddStringToObject(root, "elevatorNo", elevator_deviceid());

    cJSON* arr = cJSON_AddArrayToObject(root, "RecordBean");
    for (size_t i = 0; i < count; i++) {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddItemToArray(arr, item);
        // printf("%ld -> %ld : %s\n", i, r[i].timestamp, r[i].name);
        cJSON_AddNumberToObject(item, "fileIndex", i);
        cJSON_AddStringToObject(item, "fileName", r[i].name);
    }

    *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (!*payload)
        return -1;

    *len = strlen(*payload);

    HR_LOGD("publish: %s\n", (char*)*payload);

    // we should not release _record_lists
    // recordDownload maybe called later
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

    const char* serialno = elevator_serialno();  //"244200000E480001"; //elevator_deviceid();

    _iot = iot;

    snprintf(topic_command.topic, sizeof(topic_command.topic), "/API/V1/Down/%s/Command", serialno);
    uviot_topic_register(iot, &topic_command);

    snprintf(topic_command_response.topic, sizeof(topic_command_response.topic), "/API/V1/Down/%s/Command/Response", serialno);
    uviot_topic_register(iot, &topic_command_response);

    // hrbuffer_alloc(&_record_lists, sizeof(struct record) * 10);
    // uint64_t b = command_date_format_string_to_seconds("2025-05-16 07:48:30");
    // uint64_t b = command_date_format_string_to_seconds("2025-05-16 01:48:30");
    // uint64_t e = command_date_format_string_to_seconds("2025-05-16 07:58:30");
    // traverse_media_record_list(b, e);
    // hrbuffer_free(&_record_lists);
    return 0;
}

static int publish_upload_record_response() {
    return uviot_publish_async(_iot, &topic_command_response);
}

static int compare_record_by_timestamp(const void* a, const void* b) {
    const struct record* ra = (const struct record*)a;
    const struct record* rb = (const struct record*)b;
    return (int)(ra->timestamp - rb->timestamp);
}
static int traverse_media_record_list(uint64_t begin, uint64_t end) {
    char* ptr = NULL;
    char name[128] = {0};

    struct dirent* entry = NULL;
    size_t count = 0;

    DIR* dir = opendir(IPC_MEDIA_RECORD_DIR);
    if (!dir) {
        return 0;
    }
    struct record media;
    while ((entry = readdir(dir)) != NULL) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
            continue;
        }

        if ((ptr = strstr(entry->d_name, ".mp4"))) {
            memset((void*)name, 0, sizeof(name));
            strncpy(name, entry->d_name, ptr - entry->d_name);
            uint64_t ts = media_record_date_format_string_to_seconds(name);
            // ts + duration > begin && ts < end
            if (ts + MEDIA_RECORD_DURATION > begin && ts < end) {
                count++;
                // printf("==>count:%ld begin:%ld, end:%ld, time:%ld, name:%s\n", count, begin, end, ts, name);
                media.timestamp = ts;
                snprintf(media.name, sizeof(media.name), "%s", name);
                hrbuffer_append(&_record_lists, (void*)&media, sizeof(struct record));
            }
        }
    }

    closedir(dir);

    if (count != 0) {
        qsort(_record_lists.data, count, sizeof(struct record), compare_record_by_timestamp);

        // struct record* r = (struct record*)_record_lists.data;

        // for (size_t i = 0; i < count; i++) {
        //     printf("%ld -> %ld : %s\n", i, r[i].timestamp, r[i].name);
        // }
    }

    return count;
}