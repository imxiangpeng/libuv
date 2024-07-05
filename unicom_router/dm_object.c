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

#include <json-c/json_object.h>
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

    obj->parent = parent;

    if (parent) {
        hr_list_add_tail(&obj->sibling, &parent->childrens);
    } else {
        hr_list_add_tail(&obj->sibling, &_root.childrens);
    }
    return obj;
}

// create object using full path
struct dm_object* dm_object_new_ext(const char* name, enum dm_type type, dm_attribute getter, dm_attribute setter) {
    struct dm_object* object = NULL;
    char tmp[256] = {0};
    char *token = NULL, *saveptr = NULL;
    if (!name) return NULL;

    memcpy(tmp, name, strlen(name));
    saveptr = tmp;
    while ((token = strtok_r(saveptr, ".", &saveptr))) {
        int is_leaf = strlen(saveptr) == 0 ? 1 : 0;
        // this is last node/leaf
        struct dm_object* dm = dm_object_lookup(token, object);
        if (!dm) {
            // HR_LOGD("%s(%d): can not found %s create it\n", __FUNCTION__, __LINE__, tmp);
            if (is_leaf) {
                dm = dm_object_new(token, type, getter, setter, object);
            } else {
                dm = dm_object_new(token, DM_TYPE_OBJECT, dm_object_attribute, NULL, object);
            }
        }
        object = dm;
    }

    return object;
}

void dm_object_free(struct dm_object* object) {
    struct dm_object *p = NULL, *n = NULL;

    if (!object)
        object = &_root;

    hr_list_for_each_entry_safe(p, n, &object->childrens, sibling) {
        // take off p
        hr_list_del(&p->sibling);
        HR_INIT_LIST_HEAD(&p->sibling);
        dm_object_free(p);
    }

    HR_INIT_LIST_HEAD(&object->childrens);
    if (object != &_root) {
        // free it
        free(object);
    }
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
            // HR_LOGD("%s(%d): cannot find token %s parent:%p -> %s\n", __FUNCTION__, __LINE__, token, object, object->name);
            return NULL;
        }
    }

    return object;
}

struct json_object* _dm_object_object(struct dm_object* self) {
    struct dm_object* p = NULL;
    struct json_object* root = json_object_new_object();

    hr_list_for_each_entry(p, &self->childrens, sibling) {
        struct dm_value val;
        struct json_object* obj = NULL;
        memset((void*)&val, 0, sizeof(val));
        switch (p->type) {
            case DM_TYPE_STRING: {
                if (p->getter) {
                    p->getter(p, &val);
                    obj = json_object_new_string(val.val.string);
                    dm_value_reset(&val);
                } else {
                    obj = json_object_new_null();
                }
                break;
            }
            case DM_TYPE_NUMBER: {
                if (p->getter) {
                    p->getter(p, &val);
                    obj = json_object_new_int(val.val.number);
                    dm_value_reset(&val);
                } else {
                    obj = json_object_new_null();
                }
                break;
            }

            case DM_TYPE_OBJECT: {
                obj = _dm_object_object(p);
            }
            default:
                break;
        }

        if (obj) {
            json_object_object_add(root, p->name, obj);
        }
    }

    return root;
}
int dm_object_attribute(struct dm_object* self, struct dm_value* val) {
    // loop all childrens contruct json object ...
    struct json_object* root = NULL;

    if (self->type == DM_TYPE_NUMBER || self->type == DM_TYPE_STRING) {
        return self->getter(self, val);
    }

    // now we only support object
    if (self->type != DM_TYPE_OBJECT) {
        return -1;
    }

    root = _dm_object_object(self);

    // HR_LOGD("object:%s -> %s\n", self->name, json_object_to_json_string_ext(root, JSON_C_TO_STRING_PRETTY));
    dm_value_set_string_ext(val, json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN | JSON_C_TO_STRING_NOSLASHESCAPE), 0);

    json_object_put(root);

    return 0;
}

// using recursion generate full id, seperated with .
int dm_object_id(struct dm_object* self, char* id, int len) {
    struct dm_object* o = NULL;
    char tmp[128] = {0};
    if (!self || !id) {
        return -1;
    }
    if (self->parent != NULL /*&& self->parent != &_root*/) {
        int rc = dm_object_id(self->parent, id, len);
        // printf("rc:%d, %s  -> tmp:%s\n", rc, self->name, tmp);
        snprintf(id + rc, len - rc, ".%s", self->name);
    } else {
        snprintf(id, len, "%s", self->name);
    }

    return strlen(id);
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
