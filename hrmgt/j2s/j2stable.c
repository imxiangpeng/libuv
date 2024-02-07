
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include "j2stable.h"

#include <cjson/cJSON.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#define HRTBL_BASE_DB_PATH "./hrtbls"  //"./hrtbls"

static int _j2stable_init(struct j2stable *tbl) {
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
        //printf("%s(%d): failed ...\n", __FUNCTION__, __LINE__);
    }

    return 0;
}

// db: database name
struct j2stable *j2stable_init(const char *name,
                         struct j2sobject_prototype *proto) {
    struct j2stable *tbl = NULL;
    if (!name || !proto || !proto->ctor || 0 == proto->size) {
        return NULL;
    }

    printf("%s(%d): .......\n", __FUNCTION__, __LINE__);

    tbl = (struct j2stable *)calloc(1, sizeof(struct j2stable));
    if (!tbl) {
        return NULL;
    }

    // must init head
    J2SOBJECT(&tbl->object)->type = J2S_ARRAY;
    J2SOBJECT(&tbl->object)->proto = proto;
    J2SOBJECT(&tbl->object)->next = J2SOBJECT(&tbl->object)->prev = J2SOBJECT(&tbl->object);
    // real fields id started from 1
    tbl->object.__id__ = 0;

    if (asprintf(&tbl->path, "%s/%s.json", HRTBL_BASE_DB_PATH, name) < 0) {
        free(tbl);
        return NULL;
    }

    _j2stable_init(tbl);

    return tbl;
}

void j2stable_deinit(struct j2stable *tbl) {
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

    memset((void *)tbl, 0, sizeof(struct j2stable));

    free(tbl);
}

int j2stable_empty(struct j2stable *tbl) {
    if (!tbl) return 0;

    if (J2SOBJECT(&tbl->object)->next == J2SOBJECT(&tbl->object)) return 1;

    return 0;
}

static int _j2stable_commit(struct j2stable *tbl) {
    if (!tbl)
        return -1;

    // do nothing when no changes
    if (tbl->state == 0) {
        return 0;
    }

    return j2sobject_serialize_file(J2SOBJECT(&tbl->object), tbl->path);
}
int j2stable_update(struct j2stable *tbl, struct j2stbl_object *self) {
    if (!tbl || !self)
        return -1;

    tbl->state |= J2STBL_OPBIT_INSERT;
    return 0;
}

// after insert you should not free it again, it will be auto free when posible
int j2stable_insert(struct j2stable *tbl, struct j2stbl_object *self) {
    struct j2sobject *e = NULL;
    if (!tbl || !self)
        return -1;

    // if (j2stable_empty(tbl))
    //    _j2stable_init(tbl);
    // 1. target self is already on object list?
    // you should call update when it's exits!
    printf("self:%p\n", self);

    for (e = J2SOBJECT(&tbl->object)->next; e != J2SOBJECT(&tbl->object); e = e->next) {
        printf("object:%p\n", e);
        printf("object:%d\n", J2STBL_OBJECT_SELF(e)->__id__);
        if (e == J2SOBJECT(self)) {
            printf("Insert: Error target is already exist!\n");
            return -1;
        }
    }

    // 2. only access new self object
    J2SOBJECT(&tbl->object)->prev->next = J2SOBJECT(self);
    J2SOBJECT(self)->prev = J2SOBJECT(&tbl->object)->prev;
    J2SOBJECT(self)->next = J2SOBJECT(&tbl->object);
    J2SOBJECT(&tbl->object)->prev = J2SOBJECT(self);

    self->__id__ = J2STBL_OBJECT_SELF(J2SOBJECT(self)->prev)->__id__ + 1;

    for (e = J2SOBJECT(&tbl->object)->next; e != J2SOBJECT(&tbl->object); e = e->next) {
        printf("2 object:%p, next:%p\n", e, e->next);
        printf("2 object __id__: %d\n", J2STBL_OBJECT_SELF(e)->__id__);
    }
    tbl->state |= J2STBL_OPBIT_INSERT;
    // 3 schedule task to flush data to persist...
    _j2stable_commit(tbl);
    return 0;
}
