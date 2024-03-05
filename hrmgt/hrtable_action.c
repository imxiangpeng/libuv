#include <dlfcn.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <uv.h>

#include "j2stable.h"

#define DATABASE_TABLE_DIR "./j2stbls"

#define J2STBL_TRIGGER_DIR "/usr/lib/j2stbls_triggers"

#ifndef HR_LOGE
#define HR_LOGE printf
#endif

#define SAMPLE_DEMO 1

struct _queue {
    struct _queue *next;
    struct _queue *prev;
};

struct hrtbl_trigger {
    const char *table;  // table name
    void (*reload)(const char *);
};

struct hrtbl_trigger_node {
    struct hrtbl_trigger trigger;
    struct _queue queue;
};

static struct _queue _trigger_queue = {&_trigger_queue, &_trigger_queue};

static int _queue_init(struct _queue *q) {
    q->next = q->prev = q;
    return 0;
}
static int _queue_empty(struct _queue *q) {
    return q->next == q;
}

static void _queue_remove(struct _queue *q) {
    q->prev->next = q->next;
    q->next->prev = q->prev;
}
static inline void _queue_insert_tail(struct _queue *h,
                                      struct _queue *q) {
    q->next = h;
    q->prev = h->prev;
    q->prev->next = q;
    h->prev = q;
}

static void *_load_plugin(const char *plugin_so) {
    if (!plugin_so) return NULL;

    return dlopen(plugin_so, RTLD_NOW | RTLD_GLOBAL);
}

static int _load_plugins(const char *path) {
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

        if (len < 4 || entry->d_name[len - 1] != 'o' || entry->d_name[len - 2] != 's') continue;

        snprintf(plugin_so, sizeof(plugin_so), "%s/%s", path, entry->d_name);

        handle = _load_plugin(plugin_so);
        if (!handle) {
            char *err = dlerror();
            HR_LOGE("load %s failed: %s\n", plugin_so, err);
        } else {
            HR_LOGE("load %s success!\n", plugin_so);
      struct hrtbl_trigger * sym = (struct hrtbl_trigger *)dlsym(handle, "HRTBL_TRIGGER_MODULE_SYM");
//            trigger_node_alloc
        }
    }
    return 0;
}

#if SAMPLE_DEMO
// 获取元素 ele 在结构体中偏移量
#define _J2SOBJECT_CRONTASK_DATA_OFFSET(ele) \
    offsetof(struct j2sobject_crontask, ele)

// 获取结构体中 ele 元素大小
#define _J2SOBJECT_CRONTASK_DATA_LEN(ele) \
    sizeof(((struct j2sobject_crontask *)0)->ele)

#define _J2SOBJECT_CRONTASK_DATA_ARRAY_LEN(ele) \
    sizeof(((struct j2sobject_crontask *)0)->ele) / sizeof(((struct j2sobject_crontask *)0)->ele[0])

typedef struct j2sobject_crontask {
    J2STBL_DECLARE_OBJECT;
    int minute;
    int hour;
#if SUPPORT_NUM_ARRAY
    int week[7];
#endif
    char *cmd;
} j2sobject_crontask_t;

static int j2sobject_crontask_ctor(struct j2sobject *obj);

static struct j2sobject_prototype _j2sobject_crontask_prototype = {
    .name = "crontask",
    .type = J2S_OBJECT,
    .size = sizeof(struct j2sobject_crontask),
    .ctor = j2sobject_crontask_ctor,
    .dtor = NULL};

static struct j2sobject_fields_prototype _j2sobject_crontask_fields_prototype[] = {
    J2STBL_OBJECT_PRIV_FIELDS,
    {.name = "minute", .type = J2S_INT, .offset = _J2SOBJECT_CRONTASK_DATA_OFFSET(minute), .offset_len = 0},
    {.name = "hour", .type = J2S_INT, .offset = _J2SOBJECT_CRONTASK_DATA_OFFSET(hour), .offset_len = 0},
#if SUPPORT_NUM_ARRAY
    {.name = "week", .type = J2S_INT, .offset = _J2SOBJECT_CRONTASK_DATA_OFFSET(week), .offset_len = _J2SOBJECT_CRONTASK_DATA_ARRAY_LEN(week)},
#endif
    {.name = "cmd", .type = J2S_STRING, .offset = _J2SOBJECT_CRONTASK_DATA_OFFSET(cmd), .offset_len = 0 /*string buffer will dynamic allocated when needed*/},
    {0}};

static int j2sobject_crontask_ctor(struct j2sobject *obj) {
    if (!obj)
        return -1;
    obj->name = "crontask";
    obj->field_protos = _j2sobject_crontask_fields_prototype;
    return 0;
}

static void hrtbl_action_trigger_sample(const char *table) {
    struct j2stbl_object *data = NULL;
    printf("reload ...\n");
    // write name >> /etc/cron.hrtbls/cron.update

    struct j2stable *tbl = j2stable_init(table, &_j2sobject_crontask_prototype);
    if (!tbl) return;

    j2sobject_crontask_t *t = NULL;

    data = j2stable_query_all(tbl);

    if (data != NULL) {
        for (t = (j2sobject_crontask_t *)J2SOBJECT(data)->next; t != (j2sobject_crontask_t *)J2SOBJECT(data); t = (j2sobject_crontask_t *)J2SOBJECT(t)->next) {
            printf("tbl:%p\n", t);
            printf("tbl __id__:%d\n", J2STBL_OBJECT_SELF(t)->__id__);
            printf("tbl minute:%d\n", t->minute);
            printf("tbl hour:%d\n", t->hour);
      printf("%d %d * * * ls -l\n", t->minute, t->hour);
        }
    }
}

struct hrtbl_trigger HRTBL_TRIGGER_MODULE_SYM = {
  .table = "j2stbl_xxxx_trigger",
  .reload = hrtbl_action_trigger_sample,
};

#endif


static void _fs_event_cb_database_dir(uv_fs_event_t *handle,
                                      const char *filename, int events,
                                      int status) {
    struct _queue *q = NULL;
    struct hrtbl_trigger *trigger = NULL;
    printf("%s(%d): file:%s, events:0x%X, status:0x%X\n", __FUNCTION__, __LINE__,
           filename, events, status);
    for (q = _trigger_queue.next; q != &_trigger_queue; q = q->next) {
        char path[256] = {0};
        struct hrtbl_trigger_node *node = (struct hrtbl_trigger_node *)((char *)(q)-offsetof(struct hrtbl_trigger_node, queue));

        snprintf(path, sizeof(path), "%s.json", node->trigger.table);

        if (0 == strcmp(path, filename)) {
            trigger = &node->trigger;
            printf("match :%s \n", node->trigger.table);
            break;
        }
    }

    if (0 == strcmp("1", filename)) {
        uv_close((uv_handle_t *)handle, NULL);
        return;
    }
    if (!trigger) {
        printf("not found:%s\n", filename);
        return;
    }

    if (trigger->reload) {
        trigger->reload(trigger->table);
    }
}
static struct hrtbl_trigger_node *trigger_node_alloc() {
    struct hrtbl_trigger_node *node = (struct hrtbl_trigger_node *)calloc(1, sizeof(struct hrtbl_trigger_node));
    if (!node) return NULL;

    node->trigger.reload = hrtbl_action_trigger_sample;
    _queue_init(&node->queue);

    return node;
}

static void load_hrtbl_trigger() {
    struct hrtbl_trigger_node *node = trigger_node_alloc();
    node->trigger.table = "wifi_timer";
    _queue_insert_tail(&_trigger_queue, &node->queue);
    printf("alloc:%p\n", node);
    node = trigger_node_alloc();
    node->trigger.table = "net_timer";
    _queue_insert_tail(&_trigger_queue, &node->queue);
    printf("alloc:%p\n", node);
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    uv_loop_t *loop = uv_default_loop();
    struct _queue *n = NULL, *p = NULL;

    uv_fs_event_t event;
    load_hrtbl_trigger();

    memset((void *)&event, 0, sizeof(event));
    uv_fs_event_init(uv_default_loop(), &event);
    uv_fs_event_start(&event, _fs_event_cb_database_dir, DATABASE_TABLE_DIR, 0);

    uv_run(loop, UV_RUN_DEFAULT);

    for (p = _trigger_queue.next, n = p->next; p != &_trigger_queue; p = n, n = p->next) {
        struct hrtbl_trigger_node *node = (struct hrtbl_trigger_node *)((char *)(p)-offsetof(struct hrtbl_trigger_node, queue));
        _queue_remove(p);
        free(node);
    }

    uv_loop_close(loop);
    uv_library_shutdown();

    return 0;
}
