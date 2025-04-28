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

#ifndef _UOBJECT_H_
#define _UOBJECT_H_

#include "hr_list.h"

#ifdef __cplusplus
extern "C" {
#endif

#define UOBJECT_NAME_LEN 64

#define UOBJECT_DECLARE_OBJECT struct uobject __object__
#define UOBJECT(self) ((struct uobject*)self)

// struct ubus_object is append at end of uobject_t
#define UOBJECT_OF(ubus_obj, uobject_impl) \
    (uobject_t*)((char*)ubus_obj - sizeof(uobject_impl))

struct uobject;
struct ubus_context;
struct ubus_object;
struct blob_attr;

typedef struct {
    const char* name;  // method name
    int (*function)(struct uobject*, void* result);
} uobject_method_t;

typedef struct {
    const char* name;
    int size;
    int (*ctor)(struct uobject*);
    int (*dtor)(struct uobject*);
    // uobject_method_t *method;
} uobject_proto_t;

typedef struct uobject {
    const char* name;
    char* alias;  // xxx.i
    const uobject_proto_t* proto;

    struct ubus_context* ctx;
    struct ubus_object* object;
    struct hr_list_head l;
} uobject_t;

int uobject_init(struct ubus_context* ctx);
// proto contains default object name which will be used as ubus object name
// you can pass extra char* as alias name if you do not want use default name
uobject_t* uobject_create(const uobject_proto_t* proto, char* alias);
int uobject_destroy(uobject_t* self);
int uobject_deinit();
uobject_t* uobject_by_name(const char* name);
int uobject_notify(uobject_t* self, const char* type, struct blob_attr* msg, int timeout);
int uobject_event(uobject_t* self, const char* id, struct blob_attr* data);
int uobject_sync(const char *name, const char *options);
#ifdef __cplusplus
}
#endif
#endif  // _UOBJECT_H_
