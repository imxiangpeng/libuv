
#define _GNU_SOURCE
#define _XOPEN_SOURCE 600
#include <cjson/cJSON.h>
#include <curl/curl.h>
#include <dirent.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "hr_buffer.h"
#include "hr_log.h"
#include "iot.h"
#include "service.h"
#include "topic.h"
#include "topic_service.h"

#ifndef IPC_MEDIA_RECORD_DIR
#define IPC_MEDIA_RECORD_DIR "/media/mmcblk0p1"
#endif

// 2025-05-26_18-22-00.mp4
#define MEDIA_RECORD_DATE_STRING_FORMAT "%Y-%m-%d_%H-%M-%S"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define SVC_METHOD_PREFIX "thing.service.

struct record {
    /*uint64_t*/ time_t timestamp;  // utc use timegm not mktime
    int duration;
    char name[256];
    size_t size;
};

static pthread_t _upload_tid = -1;
static int _pipe_fd[2] = {-1, -1};

static char _stored_id[64] = {0};

// do not care timezone, so we can convert again
static time_t date_string_to_seconds(const char* date) {
    struct tm tm;
    // must reset tm, because strptime not fill all fields
    memset((void*)&tm, 0, sizeof(tm));
    if (strptime(date, "%Y-%m-%d", &tm) == NULL) {
        return 0;
    }

    // return mktime(&tm);
    return timegm(&tm);  // do not care timezone
}

static time_t media_record_date_format_string_to_seconds(const char* date) {
    struct tm tm;
    // must reset tm, because strptime not fill all fields
    memset((void*)&tm, 0, sizeof(tm));
    if (strptime(date, MEDIA_RECORD_DATE_STRING_FORMAT, &tm) == NULL) {
        return 0;
    }

    // return mktime(&tm);
    return timegm(&tm);  // do not care timezone
}

static int compare_record_by_timestamp(const void* a, const void* b) {
    const struct record* ra = (const struct record*)a;
    const struct record* rb = (const struct record*)b;
    return (int)(ra->timestamp - rb->timestamp);
}

static int traverse_media_record_list(uint64_t begin, uint64_t end, struct hrbuffer* lst) {
    char* ptr = NULL;

    struct stat sb;
    struct record media;
    struct dirent* entry = NULL;
    size_t count = 0;

    if (!lst) {
        return -1;
    }

    DIR* dir = opendir(IPC_MEDIA_RECORD_DIR);
    if (!dir) {
        return 0;
    }

    while ((entry = readdir(dir)) != NULL) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) {
            continue;
        }

        if ((ptr = strstr(entry->d_name, ".mp4"))) {
            int duration = MEDIA_RECORD_DURATION;
            // memset((void*)name, 0, sizeof(name));
            // strncpy(name, entry->d_name, ptr - entry->d_name);
            uint64_t ts = media_record_date_format_string_to_seconds(entry->d_name);
            if (ts == 0) {
                continue;
            }

            if (sscanf(entry->d_name, "%*d-%*d-%*d_%*d-%*d-%*d_%d", &duration) != 1) {
                continue;
            }

            // ts + duration > begin && ts < end
            if (ts + duration > begin && ts < end) {
                count++;
                // printf("==>count:%ld begin:%ld, end:%ld, time:%ld, name:%s\n", count, begin, end, ts, name);
                memset((void*)&media, 0, sizeof(media));
                media.timestamp = ts;
                media.duration = duration;
                snprintf(media.name, sizeof(media.name), "%s", entry->d_name);

                char path[512] = {0};
                snprintf(path, sizeof(path), "%s/%s", IPC_MEDIA_RECORD_DIR, entry->d_name);
                printf("elink record path:%s\n", path);
                if (0 == lstat(path, &sb)) {
                    media.size = sb.st_size;
                    printf("elink record path:%s -> %ld\n", path, media.size);
                    hrbuffer_append(lst, (void*)&media, sizeof(struct record));
                }
            }
        }
    }

    closedir(dir);

    if (count != 0) {
        qsort(lst->data, count, sizeof(struct record), compare_record_by_timestamp);

        struct record* r = (struct record*)lst->data;

        for (size_t i = 0; i < count; i++) {
            printf("%ld -> %ld : %s\n", i, r[i].timestamp, r[i].name);
        }

        // futil_write(IPC_MEDIA_RECORD_REQUEST_PLAYLIST, record_lists.data, record_lists.offset);

        // char* data = NULL;

        // ssize_t len = futil_read(IPC_MEDIA_RECORD_REQUEST_PLAYLIST, &data);

        // printf("read size:%ld\n", _record_lists.offset);
        // if (len > 0 && data) {
        //     size_t c = len / sizeof(struct record);

        //     struct record* r = (struct record*)data;

        //     for (size_t i = 0; i < c; i++) {
        //         printf("read %ld -> %ld : %s\n", i, r[i].timestamp, r[i].name);
        //     }
        // }

        // if (data) {
        //     free(data);
        //     data = NULL;
        // }
    }

    return count;
}

static int do_upload(const char* local_path, const char* remote_url) {
    CURL* curl = NULL;
    CURLcode res;
    struct curl_slist *headers = NULL;
    FILE* fp = NULL;

    if (!local_path || !remote_url) {
        return -1;
    }

    fp = fopen(local_path, "rb");
    if (!fp) {
        return -1;
    }

    
    fseek(fp, 0, SEEK_END);
    long filesize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    curl = curl_easy_init();

    if (!curl) {
        fclose(fp);
        return -1;
    }

    // snprintf(userpwd, sizeof(userpwd), "%s:%s", _options[OPTION_FTP_USERNAME].value.string, _options[OPTION_FTP_PASSWORD].value.string);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_URL, remote_url);
    curl_easy_setopt(curl, CURLOPT_READDATA, fp);
    curl_easy_setopt(curl, CURLOPT_INFILESIZE_LARGE, (curl_off_t)filesize);
    headers = curl_slist_append(headers, "Content-Type: application/octet-stream");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);   
    // curl_easy_setopt(curl, CURLOPT_FTP_CREATE_MISSING_DIRS, CURLFTP_CREATE_DIR_RETRY);
    // curl_easy_setopt(curl, CURLOPT_USERPWD, userpwd);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);
    curl_easy_setopt(curl, CURLOPT_VERBOSE, 0L);
    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        HR_LOGE("Upload %s failed: %s\n", local_path, curl_easy_strerror(res));
    } else {
        HR_LOGD("Upload successful: %s -> %s\n", local_path, remote_url);
    }


    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    fclose(fp);
    return -res;
}
static void* background_upload_thread_routin(void* args) {
    (void)args;
    char name[256] = {0};

    if (_pipe_fd[0] == -1) {
        _upload_tid = -1;
        return NULL;
    }

    while (1) {
        char* remote_url = NULL;
        char* local_path = NULL;
        size_t len = -1;
        struct stat st;
        int fd = -1;
        struct record r;
        char *ptr = NULL;
        ssize_t n = read(_pipe_fd[0], &len, sizeof(len));
        if (n <= 0) {
            continue;
        }

        printf("%s(%d): receive request id:%lu\n", __FUNCTION__, __LINE__, id);

        ptr = (char*)calloc(len, 1);
        if (!ptr) {
            size_t i = 0;
            char c;
            while(i++ < len) {
                read(_pipe_fd[0], &c, 1);
            }
            continue;
        }
        n = read(_pipe_fd[0], ptr, len);
        if (n <= 0) {
            continue;
        }

        const char* post_url = ptr;
        const char* file_name = ptr + strlen(post_url) + 1;
        
        printf("file name:%s, post url:%s\n", file_name, post_url);

        asprintf(&local_path, IPC_MEDIA_RECORD_DIR "/%s", r.name);

        if (!local_path) {
            free(ptr);
            continue;
        }

        printf("local path:%s\n", local_path);


        // upload video to ftp

        do_upload(local_path, post_url);

        free(local_path);
    }

    _upload_tid = -1;
    return NULL;
}

static struct topic _record_report_event = {
    .name = "event/RecordFileReportEvent/post",
    .topic = {0},
    .period = 0,
    .qos = 0,
    .type = TOPIC_TYPE_PUBLISH,
    .callback.on_publish = NULL,
};

// {"queryDate": "2025-08-06"}
static int _GetVideoRecordFileList(cJSON* params) {
    char* payload = NULL;
    char tmp[256] = {0};
    const char* query_date = NULL;
    if (!params) {
        return -1;
    }

    struct hrbuffer record_lists = {.data = NULL, .offset = 0, .size = 0, .preallocated = 0};

    query_date = cJSON_GetStringValue(cJSON_GetObjectItem(params, "queryDate"));
    if (!query_date) {
        return -1;
    }
    if (hrbuffer_alloc(&record_lists, sizeof(struct record) * 300) < 0) {
        // failed
        return -1;
    }
    time_t begin = date_string_to_seconds(query_date);
    time_t end = begin + 3600 * 24;
    printf("begin:%ld -> end:%ld\n", begin, end);
    int count = traverse_media_record_list(begin, end, &record_lists);

    cJSON* root = cJSON_CreateObject();
    if (!root) {
        hrbuffer_free(&record_lists);
        return -1;
    }

    snprintf(tmp, sizeof(tmp), "%d", topic_generate_mid());
    cJSON_AddStringToObject(root, "id", tmp);
    cJSON_AddStringToObject(root, "version", "1.0.0");

    cJSON* param = cJSON_AddArrayToObject(root, "params");

    printf("count:%d\n", count);
    for (int i = 0; i < count; i++) {
        struct tm tm;
        time_t t;
        struct record* r = (struct record*)record_lists.data + i;

        printf("timestamp:%ld, duration:%d, name:%s, size:%ld\n", r->timestamp, r->duration, r->name, r->size);
        cJSON* ele = cJSON_CreateObject();
        if (!ele) {
            continue;
        }

        cJSON_AddStringToObject(ele, "file_name", r->name);
        cJSON_AddNumberToObject(ele, "file_size", r->size);
        t = r->timestamp;
        (void)localtime_r(&t, &tm);
        /*size_t size =*/strftime(tmp, sizeof(tmp), "%Y-%m-%d %H:%M:%S", &tm);
        cJSON_AddStringToObject(ele, "start_time", tmp);
        memset((void*)tmp, 0, sizeof(tmp));
        t += 3600 * 24;
        (void)localtime_r(&t, &tm);
        /*size_t size =*/strftime(tmp, sizeof(tmp), "%Y-%m-%d %H:%M:%S", &tm);
        cJSON_AddStringToObject(ele, "end_time", tmp);
        cJSON_AddItemToArray(param, ele);
    }

    hrbuffer_free(&record_lists);

    payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!payload) {
        return -1;
    }

    HR_LOGD("publish: %s\n", (char*)payload);

    iot_topic_publish(&_record_report_event, payload, strlen(payload));

    free(payload);

    return 0;
}

// {"post_url": "http://127.0.0.1:48090/admin-api/thy/video/upload/5CB4E213B8CB/4?token=cVSjljqojsLz2BVQMtHvLPCIWZyb6B8L","file_name": "20250806155457_20250806155957.mp4"}
static int _UploadVideoRecordFile(cJSON* params) {
    const char* post_url = NULL;
    const char* file_name = NULL;
    if (!params) {
        return -1;
    }

    post_url = cJSON_GetStringValue(cJSON_GetObjectItem(params, "post_url"));
    file_name = cJSON_GetStringValue(cJSON_GetObjectItem(params, "file_name"));
    if (!post_url || !file_name) {
        return -1;
    }

    // size + string + \0 + string + \0
    int len = sizeof(int) + strlen(post_url) + 1 + strlen(file_name) + 1;
    char* b = (char*)calloc(len, 1);
    if (!b) {
        // memory will be freed in parent
        return -1;
    }

    char* ptr = b;
    *(int*)ptr = strlen(post_url);
    ptr = ptr + sizeof(int);
    strcpy(ptr, post_url);
    ptr += strlen(post_url);
    ptr++;
    strcpy(ptr, file_name);

    // upload record in background;
    write(_pipe_fd[1], (void*)b, len);

    return 0;
}

static struct svc_action _download_action_tbl[] = {
    {"GetVideoRecordFileList", _GetVideoRecordFileList},
    {"UploadVideoRecordFile", _UploadVideoRecordFile},
    {NULL, NULL},  // keep it
};

static int _on_download_message(void* payload, int len) {
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

    for (act = &_download_action_tbl[0]; act->name != NULL; act++) {
        if (!strcmp(act->name, method)) {
            /*int rc =*/act->method(params);
            break;
        }
    }

    cJSON_Delete(root);

    return 0;
}

int topic_download_init(const char* public_key, const char* device_name) {
    if (!public_key || !device_name) {
        return -1;
    }

    pipe(_pipe_fd);

    pthread_attr_init(&attr);

    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&_upload_tid, &attr, background_upload_thread_routin, NULL);

    // subscribe
    struct topic* t = (struct topic*)calloc(1, sizeof(struct topic) * svc_action_tbl_size);
    if (!t) {
        return -1;
    }

    for (size_t i = 0; i < sizeof(_download_action_tbl) / sizeof(_download_action_tbl[0]); i++) {
        if (!svc_action_tbl[i].name) {
            continue;
        }
        snprintf(t[i].name, sizeof(t[i].name), "service/%s", svc_action_tbl[i].name);
        snprintf(t[i].topic, sizeof(t[i].topic), "/sys/%s/%s/thing/%s", public_key, device_name, t[i].name);

        t[i].type = TOPIC_TYPE_SUBSCRIBE,
        t[i].callback.on_message = _on_download_message,
        iot_topic_register(&t[i]);
    }

    // publish event
    snprintf(_record_report_event.name, sizeof(_record_report_event.name), "service/%s", _record_report_event.name);
    snprintf(_record_report_event.topic, sizeof(_record_report_event.topic), "/sys/%s/%s/thing/%s", public_key, device_name, _record_report_event.name);
    iot_topic_register(&_record_report_event);

    return 0;
}
