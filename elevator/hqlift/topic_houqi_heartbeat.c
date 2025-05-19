// mxp, 20250502, implement houqi topic: /API/V1/Up/HeartBeat
// auto publish every 10s


#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/vfs.h>
#include <time.h>
#include <unistd.h>
#include <uv.h>

#include "elevator.h"
/// publish every 10s
#include "cjson/cJSON.h"
#include "hr_log.h"
#include "time_utils.h"
#include "uviot.h"

#define DEVICE_MEMORY_SIZE_G "1"

#define EVENT_HEARTBEAT_TOPIC_NAME "HeartBeat"

#define MAX_STR_SZ 256
#define MEMINFO_PATH "/proc/meminfo"

#define PROC_STAT "/proc/stat"

#define STORAGE_BLOCK_DEV_PRENAME "/dev/mmcblk"

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

typedef struct {
    unsigned long long user, nice, system, idle, iowait, irq, sirq, stolen, guest,
        gnice;
} cpu_jiffies_t;

static struct uviot* _iot = NULL;

static int _calc_storage_stat(int* total, int* avail, int* percent) {
    FILE* f = NULL;
    int match;
    char block_dev[256] = {0};
    char mount_dir[256] = {0};
    struct statfs stat;

    if (!total || !avail || !percent) {
        return -1;
    }

    *total = 0;
    *avail = 0;
    *percent = 0;

    f = fopen("/proc/mounts", "r");
    if (!f) {
        return -1;
    }

    do {
        match = fscanf(f, "%s %s %*s %*s %*d %*d\n", block_dev, mount_dir);
        block_dev[255] = 0;
        mount_dir[255] = 0;
        if (match != 2)
            break;
        if (!strncmp(block_dev, STORAGE_BLOCK_DEV_PRENAME,
                     strlen(STORAGE_BLOCK_DEV_PRENAME))) {
            if (statfs(mount_dir, &stat) < 0) {
                continue;
            }
            *total += ((stat.f_bsize * stat.f_blocks) >> 10);
            *avail += ((stat.f_bsize * stat.f_bfree) >> 10);
        }
    } while (match != EOF);

    fclose(f);

    if (*total == 0 || *total < *avail) {
        return -1;
    }

    *percent = (*total - *avail) * 100 / *total;
    return 0;
}

static int _read_file(const char* path, char* data, size_t size) {
    int fd = -1;
    char* ptr = NULL;

    fd = open(path, O_RDONLY);
    if (fd < 0) {
        return -1;
    }

    ptr = data;

    size_t remaining = size;
    while (remaining > 0) {
        ssize_t n = TEMP_FAILURE_RETRY(read(fd, ptr, remaining));
        if (n <= 0) {
            break;
        }
        ptr += n;
        remaining -= n;
    }
    close(fd);

    data[size - 1] = '\0';
    return size - remaining;
}
static int _get_cpu_jiffies(cpu_jiffies_t* jifs) {
    // only get first line
    char buf[1024] = {0};
    int ret = 0;

    int size = _read_file(PROC_STAT, buf, sizeof(buf));
    if (size < 0) {
        return -1;
    }

    ret = sscanf(buf, "cpu %llu %llu %llu %llu %llu %llu %llu %llu %llu %llu",
                 &jifs->user, &jifs->nice, &jifs->system, &jifs->idle,
                 &jifs->iowait, &jifs->irq, &jifs->sirq, &jifs->stolen,
                 &jifs->guest, &jifs->gnice);

    // must got first 8 items
    if (ret < 8) {
        return -1;
    }

    return 0;
}
static int _cpu_usage_percent() {
#define CPUUSAGE_CAL_PERIOD (100 * 3)  // jiffies tick

    static unsigned long long lastTotal = 0;
    static unsigned long long lastIdel = 0;
    static int lastUsage = 1;
    unsigned long long used = 0;
    unsigned long long crtTotal = 0;
    unsigned long long crtIdel = 0;
    cpu_jiffies_t jifs;
    int usage = 0;

    // cpu rate
    _get_cpu_jiffies(&jifs);
    crtTotal = jifs.user + jifs.nice + jifs.system + jifs.idle + jifs.iowait +
               jifs.irq + jifs.sirq;
    crtIdel = jifs.idle;
    if ((crtTotal - lastTotal) >= CPUUSAGE_CAL_PERIOD) {
        used = (crtTotal - lastTotal) - (crtIdel - lastIdel);
        usage = (used * 100) / (crtTotal - lastTotal);
        lastIdel = crtIdel;
        lastTotal = crtTotal;
        lastUsage = usage;
    } else {
        usage = lastUsage;
    }

    return usage;
}

static void print_memory_usage() {
    FILE* f = fopen("/proc/self/status", "r");
    if (!f)
        return;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "VmRSS:", 6) == 0 || strncmp(line, "VmSize:", 7) == 0) {
            HR_LOGD("%s", line);
        }
    }
    fclose(f);
}
static int _on_publish(void** payload, int* len) {
    printf("heartbeat publish \n");
    char tmp[256] = {0};
    int total = 0;
    int avail = 0;
    int percent = 0;

    cJSON* root = cJSON_CreateObject();
    if (!root)
        return -1;

    _calc_storage_stat(&total, &avail, &percent);

    // printf("total:%d, avail:%d, percent:%d\n", total, avail, percent);
    // total >>= 20; // 1024 * 1024 KB->GB
    // avail >>= 20; // 1024 * 1024 KB->GB
    total = round(total * 1.0f / 1024 / 1024);
    avail = round(avail * 1.0f / 1024 / 1024);

    printf("total:%d, avail:%d, percent:%d\n", total, avail, percent);

    cJSON_AddStringToObject(root, "type", "HeartBeat");
    // houqi's macAddr is serialno, length must > 12
    cJSON_AddStringToObject(root, "macAddr", elevator_serialno() /*uviot_get_connection_mac_address(_iot)*/);
    cJSON_AddStringToObject(root, "elevatorNo", elevator_deviceid());
    cJSON_AddStringToObject(root, "memory", DEVICE_MEMORY_SIZE_G);
    snprintf(tmp, sizeof(tmp), "%d", _cpu_usage_percent());
    cJSON_AddStringToObject(root, "cpu", tmp);
    snprintf(tmp, sizeof(tmp), "%d", percent);
    cJSON_AddStringToObject(root, "diskUsage", tmp);
    snprintf(tmp, sizeof(tmp), "%d", avail);
    cJSON_AddStringToObject(root, "diskLeftSpace", tmp);
    snprintf(tmp, sizeof(tmp), "%d", total);
    cJSON_AddStringToObject(root, "diskTotalSpace", tmp);
    //
    cJSON_AddNumberToObject(root, "timeStamp", get_realtime_ms());
    cJSON_AddStringToObject(root, "ipAddr", uviot_get_connection_ipv4_address(_iot));	

    *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!*payload)
        return -1;

    *len = strlen(*payload);

    print_memory_usage();
    return 0;
}

static struct uviot_topic dm_topic_heartbeat = {
    .name = EVENT_HEARTBEAT_TOPIC_NAME,
    .topic = "/API/V1/Up/" EVENT_HEARTBEAT_TOPIC_NAME,
    .period = 10 * 1000,
    .type = TOPIC_TYPE_PUBLISH,
    .callback.on_publish = _on_publish,
};

int topic_houqi_heartbeat_init(struct uviot* iot, const char* public_key, const char* device_name) {
    (void)public_key;
    (void)device_name;
    _iot = iot;
    uviot_topic_register(iot, &dm_topic_heartbeat);

    return 0;
}
