#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#endif

#include "hr_list.h"
#include "hr_log.h"

// node
enum type {
    TYPE_OBJECT,  // object
    TYPE_INT,     // value
    TYPE_STRING,  // value
};

struct value {
    // struct val_type t;
    union {
        int num;
        char* string;
    } val;
};

struct DataModelObject;
typedef int (*attributer)(struct DataModelObject* self, struct value* val);
struct DataModelObject {
    enum type type;  // 0 leaf, 1 node
    // struct value val;

    struct hr_list_head childrens;
    struct hr_list_head sibling;

    attributer getter;
    attributer setter;

    char name[];  // must at end
};

struct root {
    const char* name;
    struct hr_list_head childens;
};

struct DataModelObject _root = {
    .name = "/",
    .type = TYPE_OBJECT,
    .childrens = HR_LIST_HEAD_INIT(_root.childrens),
    .sibling = HR_LIST_HEAD_INIT(_root.sibling)};
struct DataModelObject* allocate_node(const char* name, enum type type, attributer getter, attributer setter, struct DataModelObject* parent) {
    struct DataModelObject* n = NULL;
    if (!name) return NULL;

    // append name at end of struct, reduce allocate times
    n = (struct DataModelObject*)calloc(1, sizeof(struct DataModelObject) + strlen(name) + 1);
    if (!n) return NULL;

    n->type = type;

    HR_INIT_LIST_HEAD(&n->childrens);
    HR_INIT_LIST_HEAD(&n->sibling);

    n->getter = getter;
    n->setter = setter;

    memcpy(n->name, name, strlen(name));

    if (parent) {
        hr_list_add_tail(&n->sibling, &parent->childrens);
    }
    return n;
}

// used to get all childen elements
static int common_object_getter(struct DataModelObject* self, struct value* val) {
    HR_LOGD("%s(%d): this %p, val:%p\n", __FUNCTION__, __LINE__, self, val);
    return 0;
}

static int X_CU_LockEnable_getter(struct DataModelObject* self, struct value* val) {
    HR_LOGD("%s(%d): this %p, val:%p\n", __FUNCTION__, __LINE__, self, val);
    val->val.num = 1;
    return 0;
}

static int X_CU_LockEnable_setter(struct DataModelObject* self, struct value* val) {
    HR_LOGD("%s(%d): this %p, val:%p\n", __FUNCTION__, __LINE__, self, val);
    return 0;
}

static int SerialNumber_getter(struct DataModelObject* self, struct value* val) {
    HR_LOGD("%s(%d): this %p, val:%p\n", __FUNCTION__, __LINE__, self, val);
    val->val.string = strdup("1213294392473294392");
    return 0;
}

// query object using string from parent
// parent will be redirect to _root when it's null
static struct DataModelObject* DataModelObject_lookup(const char* query, struct DataModelObject* parent) {
    char *token = NULL, *saveptr = NULL;
    struct DataModelObject* object = NULL;

    char name[256] = {0};

    if (!query) return NULL;

    if (!parent)
        parent = &_root;

    if (strlen(query) > sizeof(name) - 1) {
        HR_LOGD("%s(%d): query too long:%d >= %d\n", strlen(query), sizeof(name) - 1);
        return NULL;
    }

    memcpy(name, query, strlen(query));

    saveptr = name;

    object = parent;
    while ((token = strtok_r(saveptr, ".", &saveptr))) {
        HR_LOGD("%s(%d): token %s parent:%p -> %s\n", __FUNCTION__, __LINE__, token, parent, parent->name);
        struct DataModelObject* p = NULL;
        int found = 0;
        hr_list_for_each_entry(p, &object->childrens, sibling) {
            if (!strcmp(p->name, token)) {
                object = p;
                found = 1;
                break;
            }
        }
        if (!found) {
            HR_LOGD("%s(%d): cannot find token %s parent:%p -> %s\n", __FUNCTION__, __LINE__, token, object, object->name);
            return NULL;
        }
        HR_LOGD("%s(%d): found object %p -> %s\n", __FUNCTION__, __LINE__, object, object->name);
    }

    printf("token:%s rest:%s\n", token, saveptr);

    return object;
}

int main(int argc, char** argv) {
    printf("%s(%d): ......\n", __FUNCTION__, __LINE__);
    struct DataModelObject* Device = allocate_node("Device", TYPE_OBJECT, NULL, NULL, &_root);
    struct DataModelObject* X_CU_LockEnable = allocate_node("X_CU_LockEnable", TYPE_INT, X_CU_LockEnable_getter, X_CU_LockEnable_setter, Device);
    struct DataModelObject* DeviceInfo = allocate_node("DeviceInfo", TYPE_OBJECT, common_object_getter, NULL, Device);
    struct DataModelObject* SerialNumber = allocate_node("SerialNumber", TYPE_STRING, SerialNumber_getter, NULL, DeviceInfo);

    struct DataModelObject* o = DataModelObject_lookup("Device.X_CU_LockEnable", NULL);

    printf("o:%p\n", o);
    if (o) {
        struct value val;
        o->getter(o, &val);
        printf("lock enable:%d\n", val.val.num);
    }

    o = DataModelObject_lookup("Device.DeviceInfo.SerialNumber", NULL);
    printf("o:%p\n", o);
    if (o) {
        struct value val;
        o->getter(o, &val);
        printf("serial:%s\n", val.val.string);
    }

    o = DataModelObject_lookup("DeviceInfo.SerialNumber", Device);
    printf("o:%p\n", o);
    if (o) {
        struct value val;
        o->getter(o, &val);
        printf("serial:%s\n", val.val.string);
    }

    return 0;
}
