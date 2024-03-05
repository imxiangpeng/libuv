/*
 * Copyright (C) 2024 Inspur Group Co., Ltd. Unpublished
 *
 * Inspur Group Co., Ltd.
 * Proprietary & Confidential
 *
 * This source code and the algorithms implemented therein constitute
 * confidential information and may comprise trade secrets of Inspur
 * or its associates, and any use thereof is subject to the terms and
 * conditions of the Non-Disclosure Agreement pursuant to which this
 * source code was originally received.
 */

#include <stdio.h>
#include "j2stable.h"
#include <stddef.h>
#include "hrtbl_schedule.h"

#define SUPPORT_NUM_ARRAY 1

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


static void _reload(const char *table) {

    struct j2stbl_object *data = NULL;
  printf("this is sample plugin reload :%s ...\n", table);
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
    
    j2stable_deinit(tbl);
}
// link test
struct hrtbl_schedule_module HRTBL_TRIGGER_MODULE_SYM = {

  .table = "crontask",
  .reload = _reload
};
