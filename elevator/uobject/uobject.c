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

// mxp, 20240126, object wrapper for ubus interface
#define _GNU_SOURCE

#include "uobject.h"

#include <libubox/uloop.h>
#include <libubox/ustream.h>
#include <libubox/utils.h>
#include <stdio.h>
#include <stdlib.h>

#include "hr_log.h"
#include "libubus.h"

static struct ubus_context* _global_ubus_ctx = NULL;

static HR_LIST_HEAD(_objects_head);

int uobject_init(struct ubus_context* ctx) {
    if (!_global_ubus_ctx) {
    }

    _global_ubus_ctx = ctx;
    return 0;
}

int uobject_deinit() {
    uobject_t *p = NULL, *n = NULL;

    hr_list_for_each_entry_safe(p, n, &_objects_head, l) {
        uobject_destroy(p);
    }

    hr_list_for_each_entry_safe(p, n, &_objects_head, l) {
        uobject_destroy(p);
    }

    return 0;
}
uobject_t* uobject_create(const uobject_proto_t* proto, char* alias) {
    uobject_t* self = NULL;

    if (!proto || !proto->ctor || proto->size == 0) return NULL;

    // allocate memory once (struct + ubus_object)
    // ubus_object is at end of struct
    self = (uobject_t*)calloc(1, proto->size + sizeof(struct ubus_object));
    if (!self) return NULL;
    self->object = (struct ubus_object*)((char*)self + proto->size);

    self->proto = proto;

    // ignore return value
    (void)proto->ctor(self);

    // verify all fields have been setup correctly!
    if (!self->object->type || !self->object->methods || self->object->n_methods == 0) {
        proto->dtor(self);
        free(self);
        return NULL;
    }

    if (alias) {
        int ret = asprintf(&self->alias, "%s", alias);
        if (ret < 0) {
            proto->dtor(self);
            free(self);
            return NULL;
        }
        self->object->name = (const char*)self->alias;
    }

    HR_LOGD("%s(%d): add :%p object:%s\n", __FUNCTION__, __LINE__, self, self->object->name);

    int rc = ubus_add_object(_global_ubus_ctx, self->object);
    if (0 != rc) {
        HR_LOGE("%s(%d): add :%p object:%s failed!:%d\n", __FUNCTION__, __LINE__, self, self->object->name, rc);
        if (self->alias) {
            free(self->alias);
            self->alias = NULL;
        }
        proto->dtor(self);
        free(self);
        return NULL;
    }

    self->ctx = _global_ubus_ctx;

    hr_list_add_tail(&self->l, &_objects_head);

    return self;
}

int uobject_destroy(uobject_t* self) {
    HR_LOGD("%s(%d):object:%p -> %s(%s) will be removed\n", __FUNCTION__, __LINE__, self, self->name, self->object->name);

    ubus_remove_object(self->ctx, self->object);

    if (self->alias) {
        free(self->alias);
        self->alias = NULL;
    }

    hr_list_del(&self->l);

    free(self);
    return 0;
}

uobject_t* uobject_by_name(const char* name) {
    uobject_t* p = NULL;

    if (!name) return NULL;

    hr_list_for_each_entry(p, &_objects_head, l) {
        if (p->object && p->object->name && (0 == strcmp(name, p->object->name))) {
            return p;
        }
    }

    return NULL;
}

int uobject_notify(uobject_t* self, const char* type, struct blob_attr* msg, int timeout) {
    if (!self || !type || !msg) return -1;

    if (!self->ctx || !self->object) return -1;

    return ubus_notify(self->ctx, self->object, type, msg, timeout);
}

int uobject_event(uobject_t* self, const char* id, struct blob_attr* data) {
    struct ubus_context *ctx = _global_ubus_ctx ;
    if (!id|| !data) return -1;

    if (self && self->ctx) ctx = self->ctx;

    return ubus_send_event(ctx, id, data);
}

int uobject_sync(const char *name, const char *options) {
    char cmd[256] = {0};

    if (!name) {
        HR_LOGD("%s(%d):name is NULL\n", __FUNCTION__, __LINE__);
        return -1;
    }

    memset(cmd, 0, sizeof(cmd));
    snprintf(cmd, sizeof(cmd), "/usr/bin/ubus call %s sync \'%s\'", name, options);
    system(cmd);

    return 0;
}
