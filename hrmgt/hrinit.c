#include <assert.h>
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

#define DEFAULT_INIT_SERVICE_DIR "init.d"

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

static struct j2sobject *_service_list = NULL;

// 获取元素 ele 在结构体中偏移量
#define _J2SOBJECT_SERVICE_DATA_OFFSET(ele) \
    offsetof(struct j2sobject_service, ele)

// 获取结构体中 ele 元素大小
#define _J2SOBJECT_SERVICE_DATA_LEN(ele) \
    sizeof(((struct j2sobject_service *)0)->ele)

#define _J2SOBJECT_SERVICE_DATA_ARRAY_LEN(ele) \
    sizeof(((struct j2sobject_service *)0)->ele) / sizeof(((struct j2sobject_service *)0)->ele[0])

typedef struct j2sobject_service {
    J2SOBJECT_DECLARE_OBJECT;
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
    {.name = "name", .type = J2S_STRING, .offset = _J2SOBJECT_SERVICE_DATA_OFFSET(name), .offset_len = _J2SOBJECT_SERVICE_DATA_ARRAY_LEN(name)},
    {.name = "class", .type = J2S_STRING, .offset = _J2SOBJECT_SERVICE_DATA_OFFSET(class), .offset_len = _J2SOBJECT_SERVICE_DATA_ARRAY_LEN(class)},
    {.name = "disabled", .type = J2S_INT, .offset = _J2SOBJECT_SERVICE_DATA_OFFSET(disabled), .offset_len = 0},
    {.name = "oneshot", .type = J2S_INT, .offset = _J2SOBJECT_SERVICE_DATA_OFFSET(oneshot), .offset_len = 0},
    {.name = "argv", .type = J2S_ARRAY | J2S_STRING, .offset = _J2SOBJECT_SERVICE_DATA_OFFSET(argv), .offset_len = _J2SOBJECT_SERVICE_DATA_ARRAY_LEN(argv) /*string buffer will dynamic allocated when needed*/},
    {0}};

static int j2sobject_service_ctor(struct j2sobject *obj) {
    if (!obj)
        return -1;
    obj->name = "service";
    obj->field_protos = _j2sobject_service_fields_prototype;
    return 0;
}

static int _load_service(const char *path) {
    DIR *dir = NULL;
    struct dirent *entry = NULL;
    struct stat st;

    if (!path) return -1;

    if (stat(path, &st) || !S_ISDIR(st.st_mode)) {
        HR_LOGE("it's not a good directory ...\n");
        return -1;
    }

    dir = opendir(path);
    if (!dir) {
        HR_LOGE("can not open directory ...\n");
        return -1;
    }

    while ((entry = readdir(dir))) {
        void *handle = NULL;
        char plugin_so[512] = {0};
        size_t len = 0;

        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) continue;

        len = strlen(entry->d_name);

        snprintf(plugin_so, sizeof(plugin_so), "%s/%s", path, entry->d_name);

        printf("now path:%s\n", plugin_so);

        struct j2sobject_service *self = (struct j2sobject_service *)j2sobject_create(&_j2sobject_service_prototype);

        int ret = j2sobject_deserialize_file(J2SOBJECT(self), plugin_so);
        if (ret != 0) {
            j2sobject_free(J2SOBJECT(self));
            continue;
        }

        printf("+++++++++++++++++++++++++++\n");
        printf("name: %s\n", self->name);
        printf("class: %s\n", self->class);
        printf("disabled : %d\n", self->disabled);
        printf("oneshot: %d\n", self->oneshot);
        printf("argv:");
        for (unsigned i = 0; i < ARRAY_SIZE(self->argv); i++) {
            if (!self->argv[i]) break;
            printf(" %s", self->argv[i]);
        }
        printf("\n");
        printf("+++++++++++++++++++++++++++\n");
        // insert into tail
        J2SOBJECT(_service_list)->prev->next = J2SOBJECT(self);
        J2SOBJECT(self)->prev = J2SOBJECT(_service_list)->prev;
        J2SOBJECT(self)->next = J2SOBJECT(_service_list);
        J2SOBJECT(_service_list)->prev = J2SOBJECT(self);
    }

    closedir(dir);
    return 0;
}

int main(int argc, const char **argv) {
    (void)argc;
    (void)argv;

    _service_list = j2sobject_create_array(&_j2sobject_service_prototype);
    assert(_service_list);

    _load_service(DEFAULT_INIT_SERVICE_DIR);

    // all elements on the link will be freed
    j2sobject_free(_service_list);
    _service_list = NULL;
    return 0;
}
