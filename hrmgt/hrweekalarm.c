#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cjson/cJSON.h"
#include "j2stable.h"

#define SUPPORT_NUM_ARRAY 1
// #define SUPPORT_STRING_ARRAY 1

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
int main(int argc, char **argv) {
    struct j2stbl_object *data = NULL;

    struct j2stable *tbl = j2stable_init("hrweek", &_j2sobject_crontask_prototype);
    if (!tbl) return -1;

    j2sobject_crontask_t *t = NULL;

    data = j2stable_query_all(tbl);

    if (data != NULL) {
        for (t = (j2sobject_crontask_t *)J2SOBJECT(data)->next; t != (j2sobject_crontask_t *)J2SOBJECT(data); t = (j2sobject_crontask_t *)J2SOBJECT(t)->next) {
            printf("tbl:%p\n", t);
            printf("tbl __id__:%d\n", J2STBL_OBJECT_SELF(t)->__id__);
            printf("tbl minute:%d\n", t->minute);
            printf("tbl hour:%d\n", t->hour);
            printf("week array:\n");
            for (int i = 0; i < sizeof(t->week) / sizeof(t->week[0]); i++) {
                printf("i:%d -> %d\n", i, t->week[i]);
            }
        }
    }

    struct j2sobject_crontask *object = (struct j2sobject_crontask *)j2sobject_create(&_j2sobject_crontask_prototype);

    // 访问 struct j2sobject_crontask* 中相关字段或者作出修改

    printf("int array:\n");
    for (int i = 0; i < sizeof(object->week) / sizeof(object->week[0]); i++) {
        printf("i:%d -> %d\n", i, object->week[i]);
    }

    // 将对象重新序列化为可打印字符串
    char *str = j2sobject_serialize(J2SOBJECT(object));
    printf("serialize:%s\n", str);
    // 注意，内存必须手动释放
    free(str);

    object->minute = 30;
    object->hour= 14;

    if (object->cmd) {
        free(object->cmd);
        object->cmd = NULL;
    }
    object->cmd = strdup("ls -l /workspace");

    object->week[0] = 1;
    object->week[4] = 4;

    // 将对象重新序列化保存到文件
    j2sobject_serialize_file(J2SOBJECT(object), "j2s-week-modified.json");

    j2stable_insert(tbl, J2STBL_OBJECT_SELF(object));

    j2stable_deinit(tbl);

    return 0;
}
