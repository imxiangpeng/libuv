#include <errno.h>

#include "j2s/j2sobject.h"
#define _GNU_SOURCE
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

int _hrtbl_commit(struct hrtbl *tbl) {
    int size = 0;
    int is_array = 0;
    struct hrtbl_object *p = NULL;
    if (!tbl)
        return -1;

#if 0
  if (hr_list_empty(&tbl->cached)) {
    printf("empty do nothing ...\n");
    return 0;
  }

  // 1. self is on cached link
  hr_list_for_each_entry(p, &tbl->cached, list) {
    size++;
    printf("%s(%d): foreach :%p vs %p\n", __FUNCTION__, __LINE__, p, self);
    if (size > 1) {
      is_array = 1;
      break;
    }
  }

  if (is_array == 0) {
    p = hr_list_first_entry(&tbl->cached, struct hrtbl_object, list);
    j2sobject_serialize_file(J2SOBJECT(p), tbl->path);
  } else {
    char *data = NULL;
    cJSON *root = cJSON_CreateArray();
    hr_list_for_each_entry(p, &tbl->cached, list) {
      cJSON *ele = cJSON_CreateObject();
      j2sobject_serialize_cjson(J2SOBJECT(p), ele);
      cJSON_AddItemToArray(root, ele);
    }

    data = cJSON_Print(root); // cJSON_PrintUnformatted
    _write_file(tbl->path, data, strlen(data));
    free(data);
    cJSON_Delete(root);
  }
#endif
    return 0;
}
int hrtbl_update(struct hrtbl *tbl, struct hrtbl_object *self) {

    if (!tbl || !self)
        return -1;

    return 0;
}
int hrtbl_insert(struct hrtbl *tbl, struct j2sobject *self) {
    if (!tbl || !self)
        return -1;

    // if (hrtbl_empty(tbl))
    //    _hrtbl_init(tbl);
    return 0;
}
