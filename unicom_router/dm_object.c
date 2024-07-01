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

#include "dm_object.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hr_list.h"
#include "hr_log.h"

// static do not release!
struct dm_object _root = {
    .name = "/",
    .type = DM_TYPE_OBJECT,
    .childrens = HR_LIST_HEAD_INIT(_root.childrens),
    .sibling = HR_LIST_HEAD_INIT(_root.sibling)};

struct dm_object* dm_object_new(const char* name, enum dm_type type, dm_attribute getter, dm_attribute setter, struct dm_object* parent) {
    struct dm_object* obj = NULL;
    if (!name) return NULL;

    // append name at end of struct, reduce allocate times
    obj = (struct dm_object*)calloc(1, sizeof(struct dm_object) + strlen(name) + 1);
    if (!obj) return NULL;

    obj->type = type;

    HR_INIT_LIST_HEAD(&obj->childrens);
    HR_INIT_LIST_HEAD(&obj->sibling);

    obj->getter = getter;
    obj->setter = setter;

    memcpy(obj->name, name, strlen(name));

    if (parent) {
        hr_list_add_tail(&obj->sibling, &parent->childrens);
    } else {
        hr_list_add_tail(&obj->sibling, &_root.childrens);
    }
    return obj;
}

// query object using string from parent
// parent will be redirect to _root when it's null
struct dm_object* dm_object_lookup(const char* query, struct dm_object* parent) {
    char *token = NULL, *saveptr = NULL;
    struct dm_object* object = NULL;

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
        // HR_LOGD("%s(%d): token %s parent:%p -> %s\n", __FUNCTION__, __LINE__, token, parent, parent->name);
        struct dm_object* p = NULL;
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
        // HR_LOGD("%s(%d): found object %p -> %s\n", __FUNCTION__, __LINE__, object, object->name);
    }

    return object;
}

int dm_object_attribute(struct dm_object* self, struct dm_value* val) {
    // loop all childrens contruct json object ...
    return 0;
}

int dm_value_reset(struct dm_value* val) {
    if (!val) return 0;

    switch (val->type) {
        case DM_TYPE_STRING: {
            if (val->val.string) {
                if (val->preallocated == 0)
                    free((void*)val->val.string);
                val->val.string = NULL;
            }
            val->preallocated = 0;
            break;
        }
        default:
            break;
    }

    return 0;
}
int dm_value_set_number(struct dm_value* val, int number) {
    if (!val) return -1;

    dm_value_reset(val);
    val->type = DM_TYPE_NUMBER;
    val->val.number = number;
    return 0;
}
int dm_value_set_string(struct dm_value* val, const char* str) {
    if (!val || !str) return -1;

    dm_value_reset(val);
    val->type = DM_TYPE_STRING;
    val->val.string = strdup(str);
    val->preallocated = 0;

    return 0;
}
int dm_value_set_string_ext(struct dm_value* val, const char* str, int preallocated) {
    if (!val || !str) return -1;

    dm_value_reset(val);
    val->type = DM_TYPE_STRING;

    if (preallocated == 0)
        val->val.string = strdup(str);
    else
        val->val.string = str;

    val->preallocated = preallocated;

    return 0;
}
