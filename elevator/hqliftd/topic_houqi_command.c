// mxp, 20250502, implement houqi topic: /API/V1/Down/%s/Command
// mxp, 20250528, implement uploadRecord & recordDownload
// uploadRecord: list safe record list in file: IPC_MEDIA_RECORD_REQUEST_PLAYLIST
// response file name using: %Y%m%d%H%M%S_%Y%m%d%H%M%S, such as: 20250516074334_20250516074834
// upload record to ftp in background
// mxp, 20250604, update media record file name, format: 2025-05-26_18-22-00_duration.mp4

#define _GNU_SOURCE
// #define _XOPEN_SOURCE 600

#include <cjson/cJSON.h>
#include <curl/curl.h>
#include <dirent.h>
#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <uv.h>

#include "elevator.h"
#include "file_util.h"
#include "hr_buffer.h"
#include "hr_log.h"
#include "misc.h"
#include "option.h"
#include "uviot.h"

// #define IPC_MEDIA_RECORD_DIR "/media/mmcblk0p1"
#define IPC_MEDIA_RECORD_DIR "./media"
#define IPC_MEDIA_RECORD_REQUEST_PLAYLIST \
    IPC_MEDIA_RECORD_DIR                  \
    "/"                                   \
    ".command_upload_record_playlist"

#define IPC_CONFIG_JSON_PATH "/etc/ipc/config.json"

#define MEDIA_RECORD_DURATION 300  // 5min

// 2024-04-06 15:57:20
#define COMMAND_DATE_STRING_FORMAT "%Y-%m-%d %H:%M:%S"
// 2025-05-26_18-22-00.mp4
#define MEDIA_RECORD_DATE_STRING_FORMAT "%Y-%m-%d_%H-%M-%S"

// #define COMMAND_RTMP_URL_PREFIX "rtmp://srs.hqszjs.com:1935/live/"

// mxp, 20250609, do not send video without stop
// houqi's sim is data limited about 85G
// houqi platform calls the sendvideo command approximately every 10 seconds.
#define SENDVIDEO_COMMAND_TIMEOUT (60 * 1000)  // 1min, use _options[OPTION_LIVE_TIMEOUT].value.number * 1000
#define SENDSTATE_COMMAND_TIMEOUT (60 * 1000)  // 1min

static uv_timer_t _sendvideo_timer;
static uv_timer_t _sendstate_timer;

static int _pipe_fd[2] = {-1, -1};

static struct uviot* _iot = NULL;

struct record {
    /*uint64_t*/ time_t timestamp;  // utc use timegm not mktime
    int duration;
    char name[256];
};

static pthread_t _upload_tid = -1;

static pid_t _playback_pid = -1;

extern void topic_houqi_liftstate_report_enable(int on);

static int publish_upload_record_response();
static int traverse_media_record_list(uint64_t begin, uint64_t end);
static void* background_upload_thread_routin(void* args);

// do not block
static void _playback_process_action(pid_t pid, int status) {
    (void) status;
    // HR_LOGD("%s(%d): child :%d exited with :%d\n", __func__, __LINE__, pid, status);
    if (_playback_pid == pid) {
        _playback_pid = -1;
    }
}

// do not care timezone, so we can convert again
static time_t command_date_format_string_to_seconds(const char* date) {
    struct tm tm;
    // must reset tm, because strptime not fill all fields
    memset((void*)&tm, 0, sizeof(tm));
    if (strptime(date, COMMAND_DATE_STRING_FORMAT, &tm) == NULL) {
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

static void _stop_livertmp(void) {
    // mxp, 20250813, ipc-property-service maybe blocked!
    // which causing our loop blocked
    // replace system("ipc-property set /ipc/livertmp/enabled false");
    pid_t pid = fork();
    if (pid == 0) {
        execlp("ipc-property", "ipc-property", "set", "/ipc/livertmp/enabled", "false", NULL);
        _exit(127);
    }
}

static void _sendvideo_command_timeout(uv_timer_t* handle) {
    (void)handle;
    HR_LOGD("send video timeout, stop it\n");
    _stop_livertmp();
}

static void _sendstate_command_timeout(uv_timer_t* handle) {
    (void)handle;
    HR_LOGD("send state timeout, stop it\n");
    topic_houqi_liftstate_report_enable(0);
}

// mxp, 20250822, we parse /etc/ipc/config.json manually
// do not call ipc-property
static int _detect_ipc_livertmp_property(int* active, char** url) {
    ssize_t len = 0;
    char* data = NULL;
    cJSON *root = NULL, *ipc = NULL, *livertmp = NULL;
    const char *location = NULL, *enabled = NULL;

    if (!active || !url) {
        return -1;
    }

    len = futil_read(IPC_CONFIG_JSON_PATH, &data);
    if (len <= 0) {
        return -1;
    }

    root = cJSON_ParseWithLength(data, len);
    free(data);

    if (!root) {
        HR_LOGE("error:%s\n", cJSON_GetErrorPtr());
        return -1;
    }

    ipc = cJSON_GetObjectItem(root, "ipc");
    // it's safe when ipc is null
    livertmp = cJSON_GetObjectItem(ipc, "livertmp");

    if (!ipc || !livertmp) {
        cJSON_Delete(root);
        return -1;
    }

    location = cJSON_GetStringValue(cJSON_GetObjectItem(livertmp, "location"));
    enabled = cJSON_GetStringValue(cJSON_GetObjectItem(livertmp, "enabled"));
    if (!location || !enabled) {
        cJSON_Delete(root);
        return -1;
    }

    HR_LOGD("location: %s, enabled:%s\n", location, enabled);

    *url = strdup(location);

    if (!strcmp("true", enabled)) {
        *active = 1;
    } else {
        *active = 0;
    }

    cJSON_Delete(root);
    return 0;
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

    if (0 == strcasecmp("Sendstate", type)) {
        topic_houqi_liftstate_report_enable(1);

        uv_timer_stop(&_sendstate_timer);
        uv_timer_start(&_sendstate_timer, _sendstate_command_timeout, SENDSTATE_COMMAND_TIMEOUT, 0);

        cJSON_Delete(root);
        return 0;
    }

    if (0 == strcasecmp("Sendvideo", type)) {
        // todo
        int active = 0;
        char* url = NULL;
        int ret = -1;

        int live_timeout_msec = _options[OPTION_LIVE_TIMEOUT].value.number * 1000;

        if (live_timeout_msec < 10 * 1000) {
            live_timeout_msec = SENDVIDEO_COMMAND_TIMEOUT;
        }

        cJSON_Delete(root);

        if (!_options[OPTION_LIVE_URL].value.string) {
            return 0;
        }
        // ipc-property set /ipc/livertmp/location rtmp://srs.hqszjs.com:1935/live/LC40025120000001
        // ipc-property set /ipc/livertmp/enabled true

        // mxp, 20250609, do not restart when timer is not fired
        // mxp, 20250822, force reset when livertmp is disabled
        // because there maybe multi instance to op livertmp
        ret = _detect_ipc_livertmp_property(&active, &url);

        if (ret == 0) {
            if (active != 1 || !url ||
                !_options[OPTION_LIVE_URL].value.string ||
                0 != strcmp(url, _options[OPTION_LIVE_URL].value.string)) {
                HR_LOGE("active:%d, url:%s\n", active, url ? url : "");
                ret = -1;
            }
        }

        if (url) {
            free(url);
        }

        if (ret != 0 || !uv_is_active((uv_handle_t*)&_sendvideo_timer)) {
            char cmd[512] = {0};
            // snprintf(cmd, sizeof(cmd), "ipc-property set /ipc/livertmp/location " COMMAND_RTMP_URL_PREFIX "/%s;ipc-property set /ipc/livertmp/enabled true", elevator_serialno());
            snprintf(cmd, sizeof(cmd), "ipc-property set /ipc/livertmp/location %s;ipc-property set /ipc/livertmp/enabled true", _options[OPTION_LIVE_URL].value.string);
            // mxp, 20250813, ipc-property-service maybe blocked!
            // which causing our loop blocked
            // replace system(cmd) with following code
            pid_t pid = fork();
            if (pid == 0) {
                execl("/bin/sh", "sh", "-c", cmd, NULL);
                _exit(127);
            }
        }

        uv_timer_stop(&_sendvideo_timer);
        // stop video after SENDVIDEO_COMMAND_TIMEOUT ms
        uv_timer_start(&_sendvideo_timer, _sendvideo_command_timeout, live_timeout_msec, 0);
        return 0;
    }

    // same rtmp url with Sendvideo
    if (0 == strcasecmp("videoPlayBack", type)) {
        struct tm tm;
        time_t timestamp_begin = 0, timestamp_end = 0;
        char *start_time = NULL, *end_time = NULL, *user_id = NULL;

        char begin_str[64] = {0};
        char end_str[64] = {0};
        char url[LINE_MAX] = {0};

        if (!_options[OPTION_LIVE_URL].value.string) {
            cJSON_Delete(root);
            return -1;
        }
        // 2024-04-06 15:57:20
        start_time = cJSON_GetStringValue(cJSON_GetObjectItem(root, "startTime"));
        end_time = cJSON_GetStringValue(cJSON_GetObjectItem(root, "endTime"));
        user_id = cJSON_GetStringValue(cJSON_GetObjectItem(root, "userID"));

        if (!start_time || !end_time|| !user_id) {
            cJSON_Delete(root);
            return -1;
        }

        timestamp_begin = command_date_format_string_to_seconds(start_time);
        timestamp_end = command_date_format_string_to_seconds(end_time);
        if (timestamp_begin == 0 || timestamp_end == 0) {
            cJSON_Delete(root);
            return -1;
        }

        gmtime_r(&timestamp_begin, &tm);  // localtime_r
        strftime(begin_str, sizeof(begin_str), "%Y%m%d%H%M%S", &tm);

        gmtime_r(&timestamp_end, &tm);  // localtime_r
        strftime(end_str, sizeof(end_str), "%Y%m%d%H%M%S", &tm);

        // snprintf(url, sizeof(url), COMMAND_RTMP_URL_PREFIX "%s", elevator_serialno());
        // playback url is live_url_${userID}
        snprintf(url, sizeof(url), "%s_%s", _options[OPTION_LIVE_URL].value.string, user_id);

        // todo
        cJSON_Delete(root);

        // no need stop live rtmp playback using seperated url

        if (_playback_pid != -1) {
            HR_LOGD("playback(%d) is active, kill it's group\n", _playback_pid);
            // estreamer using it's seperated process group
            // we can not use SIGTERM estreamer can not receive
            kill(-_playback_pid, SIGKILL);  // SIGTERM
            int max = 100;
            while( _playback_pid != -1 && max-- > 0) {
                usleep(1000 * 20);
            }
            HR_LOGD("now previous playback:%d, max:%d\n", _playback_pid, max);
        }

        pid_t pid = fork();

        if (pid < 0) {
            HR_LOGE("can not start estreamer\n");
            return -1;
        }

        if (pid == 0) {  // child

            char* argv[] = {
                "/usr/bin/estreamer",
                begin_str,
                end_str,
                url,
                NULL,
            };

            for (size_t i = 0; i < sizeof(argv) / sizeof(argv[0]); i++) {
                HR_LOGD("%ld --> %s\n", i, argv[i]);
            }

            if (execvp(argv[0], argv) < 0) {
                HR_LOGE("%s(%d): can not start:%s\n", __FUNCTION__, __LINE__, argv[0]);
                exit(127);
            }

            HR_LOGD("child %s finished\n", argv[0]);
            exit(127);
        }

        _playback_pid = pid;
        HR_LOGD("timeshift video playback task: %d\n", _playback_pid);
        return 0;
    }

    //
    if (0 == strcasecmp("uploadRecord", type)) {
        uint64_t timestamp_begin = 0, timestamp_end = 0;
        char *start_time = NULL, *end_time = NULL;

        // remove old record list directly
        unlink(IPC_MEDIA_RECORD_REQUEST_PLAYLIST);
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
        if (traverse_media_record_list(timestamp_begin, timestamp_end) > 0) {
            publish_upload_record_response();
        }
        // todo
        cJSON_Delete(root);
        return 0;
    }

    //
    if (0 == strcasecmp("recordDownload", type)) {
        size_t id = -1;
        double val = cJSON_GetNumberValue(cJSON_GetObjectItem(root, "fileIndex"));
        if (isnan(val) || val < 0) {
            cJSON_Delete(root);
            return -1;
        }

        id = (int)val;

        if (_pipe_fd[1] == -1) {
            cJSON_Delete(root);
            return -1;
        }
        // upload record in background;
        write(_pipe_fd[1], (void*)&id, sizeof(id));

        // todo
        cJSON_Delete(root);
        return 0;
    }

    // this is our self command not houqi's feature
    if (0 == strcasecmp("secureTunnel", type)) {
        const char* svc_act = NULL;
        // {"action":"start", "code":"123456", "port":10022}
        const char* action = cJSON_GetStringValue(cJSON_GetObjectItem(root, "action"));
        const char* code = cJSON_GetStringValue(cJSON_GetObjectItem(root, "code"));
        if (!action || !code) {
            cJSON_Delete(root);
            return -1;
        }

        // now we do not verify totp code
        if (0 == strcmp("start", action)) {
            svc_act = "start";
        } else if (0 == strcmp("stop", action)) {
            svc_act = "stop";
        } else if (0 == strcmp("restart", action)) {
            svc_act = "restart";
        }

        cJSON_Delete(root);

        if (!svc_act) {
            return -1;
        }

        pid_t pid = fork();
        if (pid == 0) {
            execl("/etc/init.d/ssh_tunnel", "secure_tunnel", svc_act, NULL);
            _exit(127);
        }
        return 0;
    }

    cJSON_Delete(root);
    return 0;
}

static int _on_command_upload_record_response_publish(void** payload, int* len) {
    (void)payload;
    (void)len;

    struct record* r = NULL;
    size_t count = 0;

    char* data = NULL;
    ssize_t size = futil_read(IPC_MEDIA_RECORD_REQUEST_PLAYLIST, &data);

    if (size < 0 || !data) {
        return -1;
    }

    if (size % sizeof(struct record) != 0) {
        HR_LOGE("%s(%d): data maybe invalid ...\n", __FUNCTION__, __LINE__);
        return -1;
    }

    count = size / sizeof(struct record);

    r = (struct record*)data;

    cJSON* root = cJSON_CreateObject();
    if (!root) {
        free(data);
        return -1;
    }

    cJSON_AddStringToObject(root, "type", "uploadRecordList");
    // houqi's macAddr is serialno, length must > 12
    cJSON_AddStringToObject(root, "macAddr", elevator_serialno());  // elevator_mac
    cJSON_AddStringToObject(root, "elevatorNo", elevator_deviceid());

    cJSON* arr = cJSON_AddArrayToObject(root, "recordList");
    for (size_t i = 0; i < count; i++) {
        struct tm* tm = NULL;
        char name[64] = {0};
        cJSON* item = cJSON_CreateObject();
        cJSON_AddItemToArray(arr, item);
        // printf("%ld -> %ld : %s\n", i, r[i].timestamp, r[i].name);
        cJSON_AddNumberToObject(item, "fileIndex", i);
        // should we convert file name?
        tm = gmtime((const time_t*)&r[i].timestamp);
        if (tm) {
            // 20250206010000_20250206010500.mp4
            size_t s = strftime(name, sizeof(name), "%Y%m%d%H%M%S", tm);
            name[s++] = '_';
            time_t t = r[i].timestamp + r[i].duration;
            tm = gmtime((const time_t*)&t);
            s = strftime(name + s, sizeof(name) - s, "%Y%m%d%H%M%S", tm);

            // buffer large enough, it's safe
            strcat(name, ".mp4");
            cJSON_AddStringToObject(item, "fileName", name);
        } else {
            cJSON_AddStringToObject(item, "fileName", r[i].name);
        }
    }

    *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    free(data);
    data = NULL;

    if (!*payload)
        return -1;

    *len = strlen(*payload);

    HR_LOGD("publish: %s\n", (char*)*payload);

    return 0;
}
static struct uviot_topic _topic_command = {
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

static struct uviot_topic _topic_command_response = {
    .name = "Command/Response",
    .topic = {0},
    .type = TOPIC_TYPE_PUBLISH,
    .callback.on_publish = _on_command_upload_record_response_publish,
};

// some memory maybe not released, because no deinit interface
int topic_houqi_command_init(struct uviot* iot, const char* public_key, const char* device_name) {
    (void)public_key;
    (void)device_name;

    pthread_attr_t attr;

    const char* serialno = elevator_serialno();  //"244200000E480001"; //elevator_deviceid();

#if ENABLE_TOPIC_CUSTOM
    // if (!_options[OPTION_MQ_TOPIC_SUB_COMMAND].value.string ||
    //    !_options[OPTION_MQ_TOPIC_SUB_COMMAND_RESPONSE].value.string) {
    //    HR_LOGE("invalid topic ...\n");
    //    _exit(-1);
    // }
#endif

    _iot = iot;

    // ignore error
    pipe(_pipe_fd);

    pthread_attr_init(&attr);

    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&_upload_tid, &attr, background_upload_thread_routin, NULL);

    unlink(IPC_MEDIA_RECORD_REQUEST_PLAYLIST);

    // init timer for sendvideo & sendsate command timeout
    memset((void*)&_sendvideo_timer, 0, sizeof(_sendvideo_timer));
    memset((void*)&_sendstate_timer, 0, sizeof(_sendstate_timer));
    uv_timer_init(uv_default_loop(), &_sendvideo_timer);
    uv_timer_init(uv_default_loop(), &_sendstate_timer);

    snprintf(_topic_command.topic, sizeof(_topic_command.topic), "/API/V1/Down/%s/Command", serialno);
#if ENABLE_TOPIC_CUSTOM
    if (_options[OPTION_MQ_TOPIC_SUB_COMMAND].value.string) {
        snprintf(_topic_command.topic, sizeof(_topic_command.topic), "%s", _options[OPTION_MQ_TOPIC_SUB_COMMAND].value.string);
    }
#endif
    uviot_topic_register(iot, &_topic_command);

    snprintf(_topic_command_response.topic, sizeof(_topic_command_response.topic), "/API/V1/Down/%s/Command/Response", serialno);
#if ENABLE_TOPIC_CUSTOM
    if (_options[OPTION_MQ_TOPIC_PUB_COMMAND_RESPONSE].value.string) {
        snprintf(_topic_command_response.topic, sizeof(_topic_command_response.topic), "%s", _options[OPTION_MQ_TOPIC_PUB_COMMAND_RESPONSE].value.string);
    }
#endif
    uviot_topic_register(iot, &_topic_command_response);

    misc_register_child_action(_playback_process_action);
    return 0;
}

int topic_houqi_command_deinit(void) {
    uv_close((uv_handle_t*)&_sendstate_timer, NULL);
    uv_close((uv_handle_t*)&_sendvideo_timer, NULL);
    return 0;
}

static int publish_upload_record_response() {
    return uviot_publish_async(_iot, &_topic_command_response);
}

static int compare_record_by_timestamp(const void* a, const void* b) {
    const struct record* ra = (const struct record*)a;
    const struct record* rb = (const struct record*)b;
    return (int)(ra->timestamp - rb->timestamp);
}

static int traverse_media_record_list(uint64_t begin, uint64_t end) {
    char* ptr = NULL;

    struct record media;
    struct dirent* entry = NULL;
    size_t count = 0;
    struct hrbuffer record_lists = {.data = NULL, .offset = 0, .size = 0, .preallocated = 0};

    DIR* dir = opendir(IPC_MEDIA_RECORD_DIR);
    if (!dir) {
        return 0;
    }

    if (hrbuffer_alloc(&record_lists, sizeof(struct record) * 50) < 0) {
        // failed
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
                hrbuffer_append(&record_lists, (void*)&media, sizeof(struct record));
            }
        }
    }

    closedir(dir);

    if (count != 0) {
        qsort(record_lists.data, count, sizeof(struct record), compare_record_by_timestamp);

        // struct record* r = (struct record*)_record_lists.data;

        // for (size_t i = 0; i < count; i++) {
        //     printf("%ld -> %ld : %s\n", i, r[i].timestamp, r[i].name);
        // }
        futil_write(IPC_MEDIA_RECORD_REQUEST_PLAYLIST, record_lists.data, record_lists.offset);

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

    hrbuffer_free(&record_lists);

    return count;
}

static int do_upload(const char* local_path, const char* remote_url) {
    CURL* curl = NULL;
    CURLcode res;
    FILE* fp = NULL;
    char userpwd[128] = {0};

    if (!local_path || !remote_url) {
        return -1;
    }

    if (!_options[OPTION_FTP_USERNAME].value.string || !_options[OPTION_FTP_PASSWORD].value.string) {
        return -1;
    }

    fp = fopen(local_path, "rb");
    if (!fp) {
        return -1;
    }

    curl = curl_easy_init();

    if (!curl) {
        fclose(fp);
        return -1;
    }

    snprintf(userpwd, sizeof(userpwd), "%s:%s", _options[OPTION_FTP_USERNAME].value.string, _options[OPTION_FTP_PASSWORD].value.string);
    curl_easy_setopt(curl, CURLOPT_UPLOAD, 1L);
    curl_easy_setopt(curl, CURLOPT_URL, remote_url);
    curl_easy_setopt(curl, CURLOPT_READDATA, fp);
    curl_easy_setopt(curl, CURLOPT_FTP_CREATE_MISSING_DIRS, CURLFTP_CREATE_DIR_RETRY);
    curl_easy_setopt(curl, CURLOPT_USERPWD, userpwd);

    curl_easy_setopt(curl, CURLOPT_VERBOSE, 0L);
    res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        HR_LOGE("Upload %s failed: %s\n", local_path, curl_easy_strerror(res));
    } else {
        HR_LOGD("Upload successful: %s -> %s\n", local_path, remote_url);
    }

    curl_easy_cleanup(curl);
    fclose(fp);
    return -res;
}
static void* background_upload_thread_routin(void* args) {
    (void)args;
    size_t id = -1;
    char name[256] = {0};

    if (_pipe_fd[0] == -1) {
        _upload_tid = -1;
        return NULL;
    }
    while (1) {
        char* remote_url = NULL;
        char* local_path = NULL;
        struct stat st;
        int fd = -1;
        struct record r;

        ssize_t n = read(_pipe_fd[0], &id, sizeof(id));
        if (n <= 0) {
            continue;
        }

        printf("%s(%d): receive request id:%lu\n", __FUNCTION__, __LINE__, id);

        if (lstat(IPC_MEDIA_RECORD_REQUEST_PLAYLIST, &st) != 0 || !S_ISREG(st.st_mode)) {
            continue;
        }

        if (st.st_size % sizeof(struct record) != 0) {
            HR_LOGE("%s(%d): data maybe invalid ...\n", __FUNCTION__, __LINE__);
            continue;
        }

        if (id > (st.st_size / sizeof(struct record) - 1)) {
            HR_LOGE("%s(%d): id out of range %ld > %ld ...\n", __FUNCTION__, __LINE__, id, st.st_size / sizeof(struct record) - 1);
            continue;
        }
        // now we should seek to record
        fd = open(IPC_MEDIA_RECORD_REQUEST_PLAYLIST, O_RDONLY);
        if (fd < 0) {
            HR_LOGE("%s(%d): can not open playlist file ...\n", __FUNCTION__, __LINE__);
            continue;
        }

        lseek(fd, id * sizeof(struct record), SEEK_SET);

        memset((void*)&r, 0, sizeof(r));
        ssize_t size = read(fd, &r, sizeof(r));
        if (size != sizeof(r)) {
            close(fd);
            continue;
        }
        close(fd);
        fd = -1;

        if (!_options[OPTION_FTP_ADDRESS].value.string) {
            continue;
        }

        struct tm* tm = gmtime((const time_t*)&r.timestamp);
        if (!tm) {
            continue;
        }

        asprintf(&local_path, IPC_MEDIA_RECORD_DIR "/%s", r.name);

        if (!local_path) {
            continue;
        }

        printf("local path:%s\n", local_path);

        if (lstat(local_path, &st) != 0 || !S_ISREG(st.st_mode)) {
            free(local_path);
            continue;
        }

        memset((void*)name, 0, sizeof(name));

        size_t s = strftime(name, sizeof(name), "%Y%m%d%H%M%S", tm);
        name[s++] = '_';
        time_t t = r.timestamp + r.duration;
        tm = gmtime((const time_t*)&t);
        s = strftime(name + s, sizeof(name) - s, "%Y%m%d%H%M%S", tm);

        //  http://gd.hqszjs.com:910/record/GD500107001385/20250526233500_20250526234000.mp4
        asprintf(&remote_url, "%s/record/%s/%s.mp4", _options[OPTION_FTP_ADDRESS].value.string,
                 elevator_deviceid(), name);
        if (!remote_url) {
            free(local_path);
            continue;
        }
        printf("remote url:%s\n", remote_url);

        // upload video to ftp

        do_upload(local_path, remote_url);

        free(local_path);
        free(remote_url);
    }

    _upload_tid = -1;
    return NULL;
}
