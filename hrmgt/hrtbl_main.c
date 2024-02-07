#include <stddef.h>
#include <stdio.h>

#include "hr_tbl.h"
#include "j2s/j2sobject.h"

typedef struct {
    J2STBL_DECLARE_OBJECT;
    int id;
    char mac[18];
    int type;
} hrtbl_mactbl_t;

#define _MACTBL_DATA_OFFSET(ele) offsetof(hrtbl_mactbl_t, ele)
#define _MACTBL_DATA_LEN(ele) sizeof(((hrtbl_mactbl_t*)0)->ele)

static int hrtbl_mactbl_ctor(struct j2sobject* obj);

struct j2sobject_prototype hrtbl_mac_tbl_prototype = {.name = "mac_tbl",
                                                      .type = J2S_OBJECT,
                                                      .size = sizeof(
                                                          hrtbl_mactbl_t),
                                                      .ctor = hrtbl_mactbl_ctor,
                                                      .dtor = NULL

};

static struct j2sobject_fields_prototype _hrtbl_mactbl_fields_prototype[] = {
J2STBL_OBJECT_PRIV_FIELDS,
    {.name = "id", .type = J2S_INT, .offset = _MACTBL_DATA_OFFSET(id), .offset_len = 0},
    {.name = "mac", .type = J2S_STRING, .offset = _MACTBL_DATA_OFFSET(mac), .offset_len = _MACTBL_DATA_LEN(mac)},
    {.name = "type", .type = J2S_INT, .offset = _MACTBL_DATA_OFFSET(type), .offset_len = 0},
    {0}};

static int hrtbl_mactbl_ctor(struct j2sobject* obj) {
    if (!obj)
        return -1;
    obj->name = "mactbl";
    obj->field_protos = _hrtbl_mactbl_fields_prototype;
    return 0;
}

int main(int argc, char** argv) {
    (void)argc;
    (void)argv;

    struct hrtbl* tbl = hrtbl_init("mac_tbl", &hrtbl_mac_tbl_prototype);
    if (!tbl) {
        printf("can not init mac tbl\n");
        return -1;
    }
    hrtbl_mactbl_t* t = NULL;

    for (t = (hrtbl_mactbl_t*)J2SOBJECT(&tbl->object)->next; t != (hrtbl_mactbl_t*)J2SOBJECT(&tbl->object); t = (hrtbl_mactbl_t*)J2SOBJECT(t)->next) {
        printf("mac tbl:%p\n", t);
        printf("mac tbl __id__:%d\n", J2STBL_OBJECT_SELF(t)->__id__);
        printf("mac tbl id:%d\n", t->id);
        printf("mac tbl mac:%s\n", t->mac);
        printf("mac tbl type:%d\n", t->type);
    }

    hrtbl_mactbl_t* item = (hrtbl_mactbl_t*)j2sobject_create(&hrtbl_mac_tbl_prototype);
    item->id = 10;
    item->type = 1;
    snprintf(item->mac, sizeof(item->mac), "%s", "6c:0b:84:3c:71:9e");
    hrtbl_insert(tbl, J2STBL_OBJECT_SELF(item));

    hrtbl_deinit(tbl);
    return 0;
}
