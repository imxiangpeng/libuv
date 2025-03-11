
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <uv.h>

#include "dm_topic.h"
/// publish every 10s
#include "cjson/cJSON.h"
#include "hr_log.h"

#define DEVICE_MEMORY_SIZE_G "8"
#define DEVICE_STORAGE_SIZE_G "64"  // G
// static char _elevator_no[256] = {0};

#define EVENT_HEARTBEAT_TOPIC_NAME "Heartbeat"

static int _init(void) {
    // snprintf(_elevator_no, sizeof(_elevator_no), "%s", "12345678890");

    return 0;
}

static const char* _elevator_no(void) {
    return "1234567890";
}

static const char* _mac_address(void) {
    return "FA1FADCDD2SQ";
}

static int _disk_usage_percent(void) {
    return 60;
}

static int _disk_free_size(void) {
    return 60;  // G
}
static int _cpu_usage_percent(void) {
    return 60;
}

static int _interface_probe() {
    char buf[512];
    uv_interface_address_t* info;
    int count, i;

    uv_interface_addresses(&info, &count);
    i = count;

    printf("Number of interfaces: %d\n", count);
    while (i--) {
        uv_interface_address_t interface_a = info[i];

        printf("Name: %s\n", interface_a.name);
        printf("Internal? %s\n", interface_a.is_internal ? "Yes" : "No");

        if (interface_a.address.address4.sin_family == AF_INET) {
            uv_ip4_name(&interface_a.address.address4, buf, sizeof(buf));
            printf("IPv4 address: %s\n", buf);
        } else if (interface_a.address.address4.sin_family == AF_INET6) {
            uv_ip6_name(&interface_a.address.address6, buf, sizeof(buf));
            printf("IPv6 address: %s\n", buf);
        }

        printf("\n");
    }

    uv_free_interface_addresses(info, count);
}
static int _on_publish(void** payload, int* len) {
    printf("heartbeat publish \n");
    char tmp[256] = {0};
    cJSON* root = cJSON_CreateObject();
    if (!root) return -1;
    _interface_probe();

    cJSON_AddStringToObject(root, "type", "HeartBeat");
    cJSON_AddStringToObject(root, "macAddr", "HeartBeat");
    cJSON_AddStringToObject(root, "elevatorNo", _elevator_no());
    cJSON_AddStringToObject(root, "memory", DEVICE_MEMORY_SIZE_G);
    snprintf(tmp, sizeof(tmp), "%d", _cpu_usage_percent());
    cJSON_AddStringToObject(root, "cpu", tmp);
    snprintf(tmp, sizeof(tmp), "%d", _disk_usage_percent());
    cJSON_AddStringToObject(root, "diskUsage", tmp);
    snprintf(tmp, sizeof(tmp), "%d", _disk_free_size());
    cJSON_AddStringToObject(root, "diskLeftSpace", tmp);
    cJSON_AddStringToObject(root, "diskTotalSpace", DEVICE_STORAGE_SIZE_G);
    //
    snprintf(tmp, sizeof(tmp), "%ld", time(NULL));
    cJSON_AddStringToObject(root, "timeStamp", tmp);
    // ipAddr
    cJSON_AddStringToObject(root, "ipAddr", "192.168.58.1");

    *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!*payload) return -1;

    *len = strlen(*payload);
    HR_LOGD("publish: %s\n", *payload);

    return 0;
}

static struct dm_topic dm_topic_heartbeat = {
    .name = EVENT_HEARTBEAT_TOPIC_NAME,
    .topic = "/API/V1/Up/" EVENT_HEARTBEAT_TOPIC_NAME,
    .period = 10 * 1000,
    .type = TOPIC_TYPE_PUBLISH,
    .callback.on_publish = _on_publish,
};

int dm_topic_heartbeat_init(void) {
    dm_topic_register(&dm_topic_heartbeat);

    return 0;
}