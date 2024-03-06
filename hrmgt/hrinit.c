#include <dirent.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/epoll.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "hr_list.h"
#include "hrtbl_schedule.h"
#include "j2sobject.h"

#define MAX_ARGS 64

#ifndef HR_LOGE
#define HR_LOGE printf
#endif

#ifndef J2STBL_BASE_DB_PATH
#define J2STBL_BASE_DB_PATH "./j2stbls"
#endif

#define J2STBL_TRIGGER_DIR "./lib_hrtbls"

#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif

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

struct hrtbl_schedule {
    struct hrtbl_schedule_module module;
    void *dlhandle;  // dlopen handle
    struct hr_list_head list;
};

struct hrtbl_schedule_priv {
    int epoll_fd;
    int running;
    struct hr_list_head schedules;
};

static struct hrtbl_schedule_priv _priv = {
    .epoll_fd = -1,
    .running = 0,
    .schedules = {&_priv.schedules, &_priv.schedules}};

// 获取元素 ele 在结构体中偏移量
#define _J2SOBJECT_CRONTASK_DATA_OFFSET(ele) \
    offsetof(struct j2sobject_service, ele)

// 获取结构体中 ele 元素大小
#define _J2SOBJECT_CRONTASK_DATA_LEN(ele) \
    sizeof(((struct j2sobject_service *)0)->ele)

#define _J2SOBJECT_CRONTASK_DATA_ARRAY_LEN(ele) \
    sizeof(((struct j2sobject_service *)0)->ele) / sizeof(((struct j2sobject_service *)0)->ele[0])

typedef struct j2sobject_service {
    J2SOBJECT_DECLARE_OBJECT;
    int intarr[30];
    char name[24];   // service name
    char class[24];  // service class boot at which stage
    int disabled;    // auto startup
    int oneshot;     // auto startup
    // action list
    // int nargs;
    char *argv[MAX_ARGS];  // max args
} j2sobject_service_t;

static int j2sobject_service_ctor(struct j2sobject *obj);

static struct j2sobject_prototype _j2sobject_service_prototype = {
    .name = "service",
    .type = J2S_OBJECT,
    .size = sizeof(struct j2sobject_service),
    .ctor = j2sobject_service_ctor,
    .dtor = NULL};

static struct j2sobject_fields_prototype _j2sobject_service_fields_prototype[] = {
    {.name = "intarr", .type = J2S_INT | J2S_ARRAY, .offset = _J2SOBJECT_CRONTASK_DATA_OFFSET(intarr), .offset_len = _J2SOBJECT_CRONTASK_DATA_ARRAY_LEN(intarr)},
    {.name = "name", .type = J2S_STRING, .offset = _J2SOBJECT_CRONTASK_DATA_OFFSET(name), .offset_len = _J2SOBJECT_CRONTASK_DATA_ARRAY_LEN(name)},
    {.name = "class", .type = J2S_STRING, .offset = _J2SOBJECT_CRONTASK_DATA_OFFSET(class), .offset_len = _J2SOBJECT_CRONTASK_DATA_ARRAY_LEN(class)},
    {.name = "disabled", .type = J2S_INT, .offset = _J2SOBJECT_CRONTASK_DATA_OFFSET(disabled), .offset_len = 0},
    {.name = "oneshot", .type = J2S_INT, .offset = _J2SOBJECT_CRONTASK_DATA_OFFSET(oneshot), .offset_len = 0},
    {.name = "argv", .type = J2S_ARRAY | J2S_STRING, .offset = _J2SOBJECT_CRONTASK_DATA_OFFSET(argv), .offset_len = _J2SOBJECT_CRONTASK_DATA_ARRAY_LEN(argv) /*string buffer will dynamic allocated when needed*/},
    {0}};

static int j2sobject_service_ctor(struct j2sobject *obj) {
    if (!obj)
        return -1;
    obj->name = "service";
    obj->field_protos = _j2sobject_service_fields_prototype;
    return 0;
}

int main(int argc, const char **argv) {
    (void)argc;
    (void)argv;

    struct j2sobject_service *object = (struct j2sobject_service *)j2sobject_create(&_j2sobject_service_prototype);

    for (unsigned i = 0; i < ARRAY_SIZE(object->intarr); i++) {
        // object->intarr[i] = 0xeFFFFFFF;
    }

    object->intarr[0] = 6;
    object->intarr[1] = 16;
    printf("---> %p -> 0x%X\n", object->intarr, object->intarr[0]);
    printf("%s(%d): address:%p\n", __FUNCTION__, __LINE__, object->argv);
    // 将 json 文件  1.json 反序列化到对象
    j2sobject_deserialize_file(J2SOBJECT(object), "stringarr.json");

    int mm = INT_MAX;
    printf("int max:0x%X, max:0x%X\n", mm, INT_MAX);
    printf("---> %p -> 0x%X\n", object->intarr, object->intarr[0]);
    printf("nan int:0x%x\n", object->intarr[0]);
    printf("nan int:%d\n", 0x7FFFFFFF);
    printf("nan double:%f\n", NAN);
    printf("name:%s", object->name);
    printf("class:%s", object->class);
    printf("disabled:%d", object->disabled);
    printf("oneshot:%d", object->oneshot);

    printf("string array:\n");
    for (int i = 0; i < ARRAY_SIZE(object->argv); i++) {
        printf("i:%d -> %s\n", i, object->argv[i] ? object->argv[i] : "null");
    }

    {
        char *str = j2sobject_serialize(J2SOBJECT(object));
        printf("serialize:%s\n", str);
        // 注意，内存必须手动释放
        free(str);
    }
    j2sobject_free(J2SOBJECT(object));

    object = (struct j2sobject_service *)j2sobject_create(&_j2sobject_service_prototype);

    // 将对象重新序列化为可打印字符串
    // char *str = j2sobject_serialize(J2SOBJECT(object));
    // printf("serialize:%s\n", str);
    // 注意，内存必须手动释放
    // free(str);

    snprintf(object->name, sizeof(object->name), "%s", "hrupdate");
    snprintf(object->class, sizeof(object->class), "%s", "default");

    object->oneshot = 1;
    object->disabled = 1;
    object->argv[0] = strdup("hrupdate");
    object->argv[1] = strdup("-o");
    object->argv[2] = strdup("/var/update.bin");
    object->argv[3] = strdup("-r");

    object->intarr[0] = 6;
    object->intarr[1] = 16;
    j2sobject_serialize_file(J2SOBJECT(object), "stringarr.json");

    j2sobject_free(J2SOBJECT(object));
    return 0;
}
