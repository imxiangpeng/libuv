
// mxp, 20250618, elevator stream media record files
// 1. to ftp address
// 2. to rtmp address

#define _GNU_SOURCE
#define _XOPEN_SOURCE 600

#include <assert.h>
#include <dirent.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "hr_buffer.h"
#include "hr_log.h"

#define IPC_MEDIA_RECORD_DIR "/media/mmcblk0p1"

// 2025-05-26_18-22-00_duration.mp4
#define MEDIA_RECORD_DATE_STRING_FORMAT "%Y-%m-%d_%H-%M-%S"

struct record {
    /*uint64_t*/ time_t timestamp;  // utc use timegm not mktime
    int duration;
    char name[256];
    int32_t clip_start;
    int32_t clip_end;
};

static time_t media_record_date_format_string_to_seconds(const char* date) {
    struct tm tm;
    // must reset tm, because strptime not fill all fields
    memset((void*)&tm, 0, sizeof(tm));
    if (strptime(date, MEDIA_RECORD_DATE_STRING_FORMAT, &tm) == NULL) {
        return 0;
    }

    return timegm(&tm);  // do not care timezone
}

static int compare_record_by_timestamp(const void* a, const void* b) {
    const struct record* ra = (const struct record*)a;
    const struct record* rb = (const struct record*)b;
    return (int)(ra->timestamp - rb->timestamp);
}

static int traverse_media_record_list(struct hrbuffer* lists, uint64_t begin, uint64_t end, int* completed) {
    char* ptr = NULL;

    struct record media;
    struct dirent* entry = NULL;
    size_t count = 0;

    if (!completed) {
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
            int duration = 0;
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
                memset((void*)&media, 0, sizeof(media));
                media.timestamp = ts;
                media.duration = duration;
                snprintf(media.name, sizeof(media.name), "%s", entry->d_name);

                media.clip_start = 0;
                media.clip_end = 0;

                if (ts < begin) {
                    media.clip_start = begin - ts;
                }

                if (ts + duration > end) {
                    media.clip_end = end - ts;
                }
                printf("==>count:%ld begin:%ld, end:%ld, time:%ld, name:%s, clip:%d-%d\n", count, begin, end, ts, media.name, media.clip_start, media.clip_end);
                // hrbuffer_append(&record_lists, (void*)&media, sizeof(struct record));
                hrbuffer_append(lists, (void*)&media, sizeof(struct record));
            }
        }
    }
    closedir(dir);

    if (count != 0) {
        // qsort(record_lists.data, count, sizeof(struct record), compare_record_by_timestamp);
        qsort(lists->data, count, sizeof(struct record), compare_record_by_timestamp);

        // struct record* r = (struct record*)record_lists.data;
        struct record* r = (struct record*)lists->data;

        for (size_t i = 0; i < count; i++) {
            printf("%ld -> %ld : %s, clip:[%d,%d]\n", i, r[i].timestamp, r[i].name, r[i].clip_start, r[i].clip_end);
        }

        if ((uint64_t)r[count - 1].timestamp + r[count - 1].duration < end) {
            printf("maybe not finished, we should wait ...\n");
            *completed = 0;
        } else {
            *completed = 1;
        }
    }

    // hrbuffer_free(&record_lists);

    return count;
}

static int64_t date_format_string_to_seconds(const char* date) {
    struct tm tm;
    // must reset tm, because strptime not fill all fields
    memset((void*)&tm, 0, sizeof(tm));
    if (strptime(date, "%Y%m%d%H%M%S", &tm) == NULL) {
        return 0;
    }

    // return mktime(&tm);
    return timegm(&tm);  // do not care timezone
}

// stream to
// 1. ftp
// curl --ftp-pasv --retry 5 --retry-delay 5 --retry-max-time 60
// -T output.mp4 ftp://user:password@ftp.example.com/videos/output.mp4
// 2. rtmp
// 3. local

// estreamer begin_timestamp end_timestamp url
// estreamer 20250617060012 20250617060700 ftp://inspur:inspur88%2A@ftp.hqszjs.com:2100/event_files/GD500103000172/20250617_060012.mp4
int main(int argc, const char** argv) {
    (void)argc;
    (void)argv;

    // int64_t begin_ts;
    char cmd[LINE_MAX] = {0};
    char concat_list[256] = {0};
    char concat_path[256] = {0};

    FILE* fp = NULL;
    struct hrbuffer list;

    int completed = 0;
    int count = 0;
    int l = 0;

    if (argc < 4) {
        return -1;
    }

    for (int i = 0; i < argc; i++) {
        printf("%d: %s\n", i, argv[i]);
    }

    // kill all children process
    setpgid(0, 0);

    const char* begin = argv[1];
    const char* end = argv[2];
    const char* url = argv[3];

    // const char* begin = "2025-06-18 11:31:22";
    // const char* end = "2025-06-18 11:45:22";
    // discontinue
    // const char* begin = "20250612150330";
    // const char* end = "2025-06-12 15:18:20";
    // const char* begin = "2025-06-12 15:03:25";
    // const char* end = "20250612150345";
    // const char* end = "2025-06-12 15:03:40";

    // const char* begin = "2025-06-11 17:38:24";
    // const char* end = "2025-06-11 18:04:16";

    // const char* begin = "2025-06-17 06:04:19";
    // const char* end = "2025-06-17 06:06:19";

    int64_t b = date_format_string_to_seconds(begin);
    int64_t e = date_format_string_to_seconds(end);

    snprintf(concat_list, sizeof(concat_list), IPC_MEDIA_RECORD_DIR "/.estreamer_%d_list.txt", getpid());
    fp = fopen(concat_list, "w+");
    if (!fp) {
        printf("can not open file :%s\n", concat_list);
        return -1;
    }

    snprintf(concat_path, sizeof(concat_path), IPC_MEDIA_RECORD_DIR "/.estreamer_%d_%s_%s.mp4", getpid(), begin, end);

    memset((void*)&list, 0, sizeof(list));

    if (hrbuffer_alloc(&list, sizeof(struct record) * 10) < 0) {
        fclose(fp);
        unlink(concat_list);
        // failed
        return 0;
    }

    // wait 10min
    while (l++ < 60) {
        hrbuffer_reset(&list);
        count = traverse_media_record_list(&list, b, e, &completed);

        if (completed == 1) {
            break;
        }

        printf("%s(%d): traverse count:%d, completed:%d, l:%d\n", __FUNCTION__, __LINE__, count, completed, l);
        usleep(10 * 1000 * 1000);
    }

    if (count == 0) {
        fclose(fp);
        hrbuffer_free(&list);
        unlink(concat_list);
        return -1;
    }

    if (count == 1) {
        struct record* r = (struct record*)list.data;
        if (r->clip_start == 0 && r->clip_end == 0) {
            if (0 == strncmp(url, "ftp://", strlen("ftp://"))) {
                const char* user = getenv("FTP_USERNAME");
                const char* passwd = getenv("FTP_PASSWORD");
                snprintf(cmd, sizeof(cmd),
                         "curl -s --retry 5 --retry-delay 5 --retry-max-time 60 --ftp-create-dirs -T %s/%s %s -u '%s:%s'",
                         IPC_MEDIA_RECORD_DIR, r->name, url, user, passwd);

                system(cmd);

                fclose(fp);
                hrbuffer_free(&list);
                return 0;
            }
        }
    }

    for (int i = 0; i < count; i++) {
        struct record* r = (struct record*)list.data;
        printf("%d -> %ld : %s, clip:[%d,%d]\n", i, r[i].timestamp, r[i].name, r[i].clip_start, r[i].clip_end);

        fprintf(fp, "file '%s'\n", r[i].name);

        if (r[i].clip_start != 0) {
            fprintf(fp, "inpoint %d\n", r[i].clip_start);
        }

        if (r[i].clip_end != 0) {
            fprintf(fp, "outpoint %d\n", r[i].clip_end);
        }
    }

    fclose(fp);
    hrbuffer_free(&list);

    memset((void*)cmd, 0, sizeof(cmd));

    if (0 == strncmp(url, "rtmp://", strlen("rtmp://"))) {
        snprintf(cmd, sizeof(cmd), "ffmpeg -loglevel quiet -y -re -f concat -safe 0 -i %s -c copy -f flv %s;", concat_list, url);
    } else if (0 == strncmp(url, "ftp://", strlen("ftp://"))) {
        const char* user = getenv("FTP_USERNAME");
        const char* passwd = getenv("FTP_PASSWORD");
        snprintf(cmd, sizeof(cmd),
                 "ffmpeg -d -loglevel quiet -y -f concat -safe 0 -i %s -c copy -f mp4 %s;"
                 "curl -s --retry 5 --retry-delay 5 --retry-max-time 60 --ftp-create-dirs -T %s %s -u '%s:%s'",
                 concat_list, concat_path, concat_path, url, user, passwd);
    }

    system(cmd);

    unlink(concat_path);
    unlink(concat_list);
    return 0;
}
