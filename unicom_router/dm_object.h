#ifndef DM_OBJECT_H
#define DM_OBJECT_H

#include <stdint.h>

#include "hr_list.h"

enum dm_type {
    DM_TYPE_OBJECT,  // object
    DM_TYPE_NUMBER,  // value
    DM_TYPE_STRING,  // value
    DM_TYPE_BOOLEAN,  // value
    DM_TYPE_MAX,     // value
};
struct dm_value {
    enum dm_type type;
    union {
        // struct dm_object *object; // object is represent using json
        int number;
        int boolean;
        const char* string;
    } val;
    int preallocated;  // union value is preallocated, you no need free it...
};

struct dm_object;
typedef int (*dm_attribute)(struct dm_object* self, struct dm_value* val);

struct dm_object {
    enum dm_type type;

    struct dm_object *parent;
    struct hr_list_head childrens;
    struct hr_list_head sibling;

    dm_attribute getter;
    dm_attribute setter;
    // addInstance
    dm_attribute adder;
    // delInstance
    dm_attribute deleter;

    char name[];  // must at end
};

struct dm_object* dm_object_new(const char* name, enum dm_type type, dm_attribute getter, dm_attribute setter, struct dm_object* parent);
struct dm_object* dm_object_new_ext(const char* name, enum dm_type type, dm_attribute getter, dm_attribute setter);
void dm_object_free(struct dm_object* object);
struct dm_object* dm_object_lookup(const char* query, struct dm_object* parent);
struct dm_object* dm_object_parent(struct dm_object* object);

// default object getter sequence function
int dm_object_attribute(struct dm_object* self, struct dm_value* val);
// maybe you should implement your self setter object function if you want object's all data be set togger
int dm_object_id(struct dm_object *self, char *id, int len);
int dm_value_reset(struct dm_value* val);
int dm_value_set_number(struct dm_value*, int);
int dm_value_set_boolean(struct dm_value* val, int value);
int dm_value_set_string(struct dm_value*, const char*);
int dm_value_set_string_ext(struct dm_value*, const char*, int preallocated);
#endif