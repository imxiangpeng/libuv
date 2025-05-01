
#include <math.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/vfs.h>
#include <time.h>
#include <unistd.h>
#include <uv.h>

#include "iot_topic.h"
/// publish every 10s
#include "cjson/cJSON.h"
#include "hr_log.h"

#define DEVICE_MEMORY_SIZE_G "1"

#define EVENT_HEARTBEAT_TOPIC_NAME "HeartBeat"

#define MAX_STR_SZ 256
#define MEMINFO_PATH "/proc/meminfo"

#define PROC_STAT "/proc/stat"

#define STORAGE_BLOCK_DEV_PRENAME "/dev/mmcblk"

#ifndef TEMP_FAILURE_RETRY
#define TEMP_FAILURE_RETRY(exp)                                                \
  ({                                                                           \
    typeof(exp) _rc;                                                           \
    do {                                                                       \
      _rc = (exp);                                                             \
    } while (_rc == -1 && errno == EINTR);                                     \
    _rc;                                                                       \
  })
#endif

typedef struct {
  unsigned long long user, nice, system, idle, iowait, irq, sirq, stolen, guest,
      gnice;
} cpu_jiffies_t;

// static char _serialno[256] = {0};
// static char _mac[18] = {0};

static void print_memory_usage() {
    FILE *f = fopen("/proc/self/status", "r");
    if (!f) return;

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (strncmp(line, "VmRSS:", 6) == 0 || strncmp(line, "VmSize:", 7) == 0) {
            HR_LOGD("%s", line);
        }
    }
    fclose(f);
}
static int _on_publish(void **payload, int *len) {
  printf("heartbeat publish \n");

  cJSON *root = cJSON_CreateObject();
  if (!root)
    return -1;


  *payload = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  if (!*payload)
    return -1;

  *len = strlen(*payload);
  HR_LOGD("publish: %s\n", *payload);

   print_memory_usage();
  return 0;
}

static struct iot_topic dm_topic_heartbeat = {
    .name = EVENT_HEARTBEAT_TOPIC_NAME,
    .topic = "/API/V1/Up/" EVENT_HEARTBEAT_TOPIC_NAME,
    .period = 10 * 1000,
    .type = TOPIC_TYPE_PUBLISH,
    .callback.on_publish = _on_publish,
};

int topic_houqi_heartbeat_init(struct iot* iot,const char* public_key, const char* device_name) {
  (void)public_key;
  (void)device_name;
  
  iot_topic_register(iot, &dm_topic_heartbeat);

  return 0;
}
