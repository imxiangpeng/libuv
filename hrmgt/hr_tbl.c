
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <errno.h>

#include "j2s/j2sobject.h"
#include <cjson/cJSON.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "hr_tbl.h"

#define HRTBL_BASE_DB_PATH "/home/alex/workspace/workspace/libuv/libuv/build/hrtbls"  //"./hrtbls"

static int _hrtbl_init(struct hrtbl *tbl) {
    int ret = 0;
    char *path = NULL;
    struct stat st;

    if (!tbl || !tbl->path)
        return -1;

    path = tbl->path;

    if (lstat(path, &st) == 0 && st.st_size == 0) {
        // not exist, directly return empty
        return 0;
    }

    ret = j2sobject_deserialize_file(J2SOBJECT(&tbl->object), tbl->path);
    if (0 != ret) {
        printf("%s(%d): failed ...\n", __FUNCTION__, __LINE__);
    }

    return 0;
}

// db: database name
struct hrtbl *hrtbl_init(const char *name,
                         struct j2sobject_prototype *proto) {
    struct hrtbl *tbl = NULL;
    if (!name || !proto || !proto->ctor || 0 == proto->size) {
        return NULL;
    }

    printf("%s(%d): .......\n", __FUNCTION__, __LINE__);

    tbl = (struct hrtbl *)calloc(1, sizeof(struct hrtbl));
    if (!tbl) {
        return NULL;
    }

    // must init head
    J2SOBJECT(&tbl->object)->type = J2S_ARRAY;
    J2SOBJECT(&tbl->object)->proto = proto;
    J2SOBJECT(&tbl->object)->next = J2SOBJECT(&tbl->object)->prev = J2SOBJECT(&tbl->object);

    if (asprintf(&tbl->path, "%s/%s.json", HRTBL_BASE_DB_PATH, name) < 0) {
        free(tbl);
        return NULL;
    }

    if (access(tbl->path, R_OK | W_OK) != 0) {
        printf("%s(%d): .......:%s\n", __FUNCTION__, __LINE__, strerror(errno));
        printf("can not read write :%s\n", tbl->path);
    }

    // using lazy method, do not open db file directly
    // also we should using temp file when replace ...
    // how verify data? using md5sum or crc?
    // sdb->fd = open(path, R_OK | W_OK);

    printf("%s(%d): .......\n", __FUNCTION__, __LINE__);
    _hrtbl_init(tbl);

    return tbl;
}

void hrtbl_deinit(struct hrtbl *tbl) {
    struct j2sobject *p = NULL, *n = NULL;
    if (!tbl) return;

    // hrtbl.object is not allocated using j2sobject_create_array
    // so we should manual release all elements
    // loop all elements free all element
    for (p = J2SOBJECT(&tbl->object)->next, n = p->next; p != J2SOBJECT(&tbl->object); p = n, n = p->next) {
        printf("%s(%d): .....p:%p, head:%p, next:%p\n", __FUNCTION__, __LINE__, p, &tbl->object, p->next);
        p->prev->next = p->next;
        p->next->prev = p->prev;
        j2sobject_free(p);
    }

    if (tbl->path)
        free(tbl->path);

    memset((void *)tbl, 0, sizeof(struct hrtbl));

    free(tbl);
}

int hrtbl_empty(struct hrtbl *tbl) {
    if (!tbl) return 0;

    if (J2SOBJECT(&tbl->object)->next == J2SOBJECT(&tbl->object)) return 1;

    return 0;
}

static int _hrtbl_commit(struct hrtbl *tbl) {
    if (!tbl)
        return -1;

    // do nothing when no changes
    if (tbl->state == 0) {
        return 0;
    }

    return j2sobject_serialize_file(J2SOBJECT(&tbl->object), tbl->path);
}
int hrtbl_update(struct hrtbl *tbl, struct j2sobject *self) {
    if (!tbl || !self)
        return -1;

    tbl->state |= TBL_OPBIT_INSERT;
    return 0;
}

// after insert you should not free it again, it will be auto free when posible
int hrtbl_insert(struct hrtbl *tbl, struct j2sobject *self) {
    struct j2sobject *e = NULL;
    if (!tbl || !self)
        return -1;

    // if (hrtbl_empty(tbl))
    //    _hrtbl_init(tbl);
    // 1. target self is already on object list?
    // you should call update when it's exits!
    printf("self:%p\n", self);

    for (e = J2SOBJECT(&tbl->object)->next; e != J2SOBJECT(&tbl->object); e = e->next) {
        printf("object:%p\n", e);
        if (e == self) {
            printf("Insert: Error target is already exist!\n");
            return -1;
        }
    }

    // 2. only access new self object
    J2SOBJECT(&tbl->object)->prev->next = self;
    self->prev = J2SOBJECT(&tbl->object)->prev;
    self->next = J2SOBJECT(&tbl->object);
    J2SOBJECT(&tbl->object)->prev = self;

    for (e = J2SOBJECT(&tbl->object)->next; e != J2SOBJECT(&tbl->object); e = e->next) {
        printf("2 object:%p\n", e);
    }
    tbl->state |= TBL_OPBIT_INSERT;
    // 3 schedule task to flush data to persist...
    _hrtbl_commit(tbl);
    return 0;
}
