
// mxp, 20250618, stream media record files

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
#include "sconf.h"

// #define IPC_MEDIA_RECORD_DIR "/media/mmcblk0p1"
#define IPC_MEDIA_RECORD_DIR "./media"
#define IPC_MEDIA_RECORD_REQUEST_PLAYLIST \
    IPC_MEDIA_RECORD_DIR                  \
    "/"                                   \
    ".command_upload_record_playlist"

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

static int traverse_media_record_list(struct hrbuffer* lists, uint64_t begin, uint64_t end) {
    char* ptr = NULL;

    struct record media;
    struct dirent* entry = NULL;
    size_t count = 0;
    // struct hrbuffer record_lists = {.data = NULL, .offset = 0, .size = 0, .preallocated = 0};

    DIR* dir = opendir(IPC_MEDIA_RECORD_DIR);
    if (!dir) {
        return 0;
    }

    // if (hrbuffer_alloc(&record_lists, sizeof(struct record) * 50) < 0) {
    //     // failed
    //     return 0;
    // }

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
        }
    }

    // hrbuffer_free(&record_lists);

    return count;
}

static time_t command_date_format_string_to_seconds(const char* date) {
    struct tm tm;
    // must reset tm, because strptime not fill all fields
    memset((void*)&tm, 0, sizeof(tm));
    if (strptime(date, "%Y-%m-%d %H:%M:%S", &tm) == NULL) {
        return 0;
    }

    // return mktime(&tm);
    return timegm(&tm);  // do not care timezone
}

// stream to
// 1. ftp
// 2. rtmp

int main(int argc, const char** argv) {
    (void)argc;
    (void)argv;

    char cmd[LINE_MAX] = {0};
    char concat_list[256] = {0};

    FILE* fp = NULL;
    struct hrbuffer list;

    // const char* begin = "2025-06-18 11:31:22";
    // const char* end = "2025-06-18 11:45:22";
    // discontinue
    const char* begin = "2025-06-12 15:03:30";
    // const char* end = "2025-06-12 15:18:20";
    // const char* begin = "2025-06-12 15:03:25";
    const char* end = "2025-06-12 15:18:25";

    // const char* begin = "2025-06-11 17:38:24";
    // const char* end = "2025-06-11 18:04:16";

    //const char* begin = "2025-06-17 06:04:19";
    //const char* end = "2025-06-17 06:06:19";

    time_t b = command_date_format_string_to_seconds(begin);
    time_t e = command_date_format_string_to_seconds(end);

    snprintf(concat_list, sizeof(concat_list), IPC_MEDIA_RECORD_DIR "/.estream_%d_list.txt", getpid());
    fp = fopen(concat_list, "w+");
    if (!fp) {
        printf("can not open file :%s\n", concat_list);
        return -1;
    }

    memset((void*)&list, 0, sizeof(list));

    if (hrbuffer_alloc(&list, sizeof(struct record) * 50) < 0) {
        fclose(fp);
        // failed
        return 0;
    }

    int count = traverse_media_record_list(&list, b, e);

    for (int i = 0; i < count; i++) {
        struct record* r = (struct record*)list.data;
        printf("%d -> %ld : %s, clip:[%d,%d]\n", i, r[i].timestamp, r[i].name, r[i].clip_start, r[i].clip_end);

        if (r[i].clip_start != 0 || r[i].clip_end != 0) {
            char output[512] = {0};

            snprintf(output, sizeof(output), ".estream_%d_%s", getpid(), r[i].name);
            if (r[i].clip_end == 0) {
                snprintf(cmd, sizeof(cmd), "ffmpeg -y -ss %d -i " IPC_MEDIA_RECORD_DIR "/%s -c copy %s/%s", r[i].clip_start, r[i].name, IPC_MEDIA_RECORD_DIR, output);
            } else {
                snprintf(cmd, sizeof(cmd), "ffmpeg -y -ss %d -i " IPC_MEDIA_RECORD_DIR "/%s -t %d -c copy %s/%s", r[i].clip_start, r[i].name, r[i].clip_end, IPC_MEDIA_RECORD_DIR, output);
            }
            printf("cmd:%s\n", cmd);
            system(cmd);
            fprintf(fp, "file '%s'\n", output);
        } else {
            fprintf(fp, "file '%s'\n", r[i].name);
        }
    }

    fclose(fp);
    hrbuffer_free(&list);

    memset((void*)cmd, 0, sizeof(cmd));

    snprintf(cmd, sizeof(cmd), "ffmpeg -y -f concat -safe 0 -i %s -c copy -f mp4 %s", concat_list, "output.mp4");
    printf("cmd:%s\n", cmd);

    // system(cmd);

    // unlink(concat_list);
    return 0;
}