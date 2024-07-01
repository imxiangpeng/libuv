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

#ifndef DM_OBJECT_H
#define DM_OBJECT_H

#include <stdint.h>

#include "hr_list.h"

enum dm_type {
    DM_TYPE_OBJECT,  // object
    DM_TYPE_NUMBER,  // value
    DM_TYPE_STRING,  // value
    DM_TYPE_MAX,     // value
};
struct dm_value {
    enum dm_type type;
    union {
        // struct dm_object *object; // object is represent using json
        int number;
        const char* string;
    } val;
    int preallocated;  // union value is preallocated, you no need free it...
};

struct dm_object;
typedef int (*dm_attribute)(struct dm_object* self, struct dm_value* val);

struct dm_object {
    enum dm_type type;

    struct hr_list_head childrens;
    struct hr_list_head sibling;

    dm_attribute getter;
    dm_attribute setter;

    char name[];  // must at end
};

struct dm_object* dm_object_new(const char* name, enum dm_type type, dm_attribute getter, dm_attribute setter, struct dm_object* parent);
struct dm_object* dm_object_lookup(const char* query, struct dm_object* parent);

// serialize object as string
int dm_object_attribute(struct dm_object* self, struct dm_value* val);

int dm_value_reset(struct dm_value* val);
int dm_value_set_number(struct dm_value*, int);
int dm_value_set_string(struct dm_value*, const char*);
int dm_value_set_string_ext(struct dm_value*, const char*, int preallocated);
#endif