#include "libubox/blobmsg.h"

#include <json-c/json.h>
#include <libubox/blobmsg_json.h>

#include "libubus.h"

#include "floor.h"
#include "hr_log.h"

#define OBJECT_NAME "elevatord"

extern struct ubus_context* uelevatord_get_ubus_ctx();

struct ubus_object _elevatord_object;

enum {
    FC_FLOOR_BASE,
    FC_FLOORS_BELOW,
    FC_FLOORS_ABOVE,
    __FC_MAX
};
static const struct blobmsg_policy _floor_calibration_policy[__FC_MAX] = {
    [FC_FLOOR_BASE] = {.name = "BaseFloor", .type = BLOBMSG_TYPE_INT32},
    [FC_FLOORS_BELOW] = {.name = "FloorsBelow", .type = BLOBMSG_TYPE_INT32},
    [FC_FLOORS_ABOVE] = {.name = "FloorsAbove", .type = BLOBMSG_TYPE_INT32},
};
// maybe we can read/write json file directly not using j2sobject
static int uobject_elevatord_property_handler(struct ubus_context* ctx, struct ubus_object* obj, struct ubus_request_data* req, const char* method, struct blob_attr* msg) {
    (void)ctx;
    (void)req;
    (void)msg;
    struct blob_buf b;

    if (!obj || !method)
        return -1;

    // uobject_t *self = UOBJECT_OF(obj, uobject_elevatord_t);

    HR_LOGD("%s(%d): method:%s\n", method);
    if (0 == strcmp("get", method)) {
        memset((void*)&b, 0, sizeof(b));
        blob_buf_init(&b, 0);

        ubus_send_reply(ctx, req, b.head);

        blob_buf_free(&b);

        return 0;
    }

    if (0 == strcmp("set", method)) {
        int result = -1;
        char* data = NULL;
        data = blobmsg_format_json(msg, true);
        HR_LOGD("%s(%d): data:%s ...\n", __FUNCTION__, __LINE__, data);
        // construct from new data
        memset((void*)&b, 0, sizeof(b));

        blob_buf_init(&b, 0);

        blobmsg_add_string(&b, "result", result == 0 ? "0" : "1");
        blobmsg_add_string(&b, "failreason", result == 0 ? "" : "Unknown Error");
        ubus_send_reply(ctx, req, b.head);
        blob_buf_free(&b);
        return 0;
    }

    // not support

    return UBUS_STATUS_INVALID_ARGUMENT;
}

// ubus -s /tmp/ubus.sock subscribe elevatord
static void _on_floor_calibration_event(int id, int floor, const char* label, double height, int completed) {
  (void)completed;
    struct ubus_context* ctx = uelevatord_get_ubus_ctx();
    if (!ctx) {
        return;
    }

    struct blob_buf b;

    memset((void*)&b, 0, sizeof(b));

    blob_buf_init(&b, 0);
    blobmsg_add_u32(&b, "Id", id);
    blobmsg_add_u32(&b, "Floor", floor);
    blobmsg_add_string(&b, "Label", label);
    blobmsg_add_double(&b, "Height", height);

    ubus_notify(ctx, &_elevatord_object, "AutoFloorCalibrationEvent", b.head, -1/*no block*/);
}

// ubus call elevatord startAutoFloorCalibration '{"BaseFloor":1, "FloorsBelow":1, "FloorsAbove":22}'
// ubus -s /tmp/ubus.sock subscribe elevatord
static int _start_auto_floor_calibration(struct ubus_context* ctx, struct ubus_object* obj, struct ubus_request_data* req, const char* method, struct blob_attr* msg) {
    (void)ctx;
    (void)obj;
    (void)req;
    (void)msg;
    (void)method;

    int floor_base = 1;
    int floors_below_base = 0;
    int floors_above_base = 0;

    struct blob_attr* tb[__FC_MAX] = {0};

    int rc = blobmsg_parse(_floor_calibration_policy, __FC_MAX, tb, blob_data(msg), blob_len(msg));

    HR_LOGD("parse:%d\n", rc);
    if (!tb[FC_FLOOR_BASE] || !tb[FC_FLOORS_BELOW] || !tb[FC_FLOORS_ABOVE]) {
        HR_LOGD("%s(%d): invalid ...\n", __FUNCTION__, __LINE__);
        return UBUS_STATUS_INVALID_ARGUMENT;
    }

    floor_base = blobmsg_get_u32(tb[FC_FLOOR_BASE]);
    floors_below_base = blobmsg_get_u32(tb[FC_FLOORS_BELOW]);
    floors_above_base = blobmsg_get_u32(tb[FC_FLOORS_ABOVE]);

    HR_LOGD("%s(%d): start auto calibration:%d %d %d\n", __FUNCTION__, __LINE__, floor_base, floors_below_base, floors_above_base);
    floor_enter_calibration_with_callback(floor_base, floors_below_base, floors_above_base, _on_floor_calibration_event);
    return 0;
}
static const struct ubus_method _object_methods[] = {
    UBUS_METHOD_NOARG("get", uobject_elevatord_property_handler),
    UBUS_METHOD("startAutoFloorCalibration", _start_auto_floor_calibration, _floor_calibration_policy),
};

static struct ubus_object_type _object_type =
    UBUS_OBJECT_TYPE(OBJECT_NAME, _object_methods);

struct ubus_object _elevatord_object = {
    .name = "elevatord",
    .type = &_object_type,
    .methods = _object_methods,
    .n_methods = ARRAY_SIZE(_object_methods),
};
