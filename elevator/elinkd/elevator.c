#include "elevator.h"

#include <stdint.h>

#include "elinkd.h"
#include "hr_log.h"
#include "libubox/blob.h"
#include "libubox/blobmsg.h"
#include "libubox/blobmsg_json.h"
#include "libubus.h"
#include "property.h"
#include "topic_property.h"

#define ELEVATORD_NAME "elevatord"

#define ELEVATORD_EVENT_REALTIME "RealTime"
#define ELEVATORD_EVENT_HISTORICAL "Historical"
#define ELEVATORD_EVENT_MOTION "Motion"

#define ELEVATORD_EVENT_SENSOR_CALIBRATION "SensorCalibration"
#define ELEVATORD_EVENT_AUTOFLOORCALIBRATIONEVENT "AutoFloorCalibrationEvent"

#define ELEVATOR_EVENT_PREFIX "elevator.event."

enum elevator_direction {
    ELEVATOR_DIR_STATIONARY = 0,
    ELEVATOR_DIR_UP,   // 1
    ELEVATOR_DIR_DOWN  // 2
};

enum elevator_door_state {
    ELEVATOR_DOOR_OPEN = 0,
    ELEVATOR_DOOR_CLOSE
};

struct elevator_status {
    enum elevator_direction direction;
    enum elevator_door_state door_state;
    double accel;
    double speed;
    double distance;
    int current_floor;
    int passenger_count;
    double jitter_freq;
    double jitter_accel;
};

static struct ubus_context* _ctx = NULL;
static uint32_t _elevatord_object_id = 0;

static char _elevator_id[128] = {0};

static struct elevator_status _status = {.door_state = ELEVATOR_DOOR_CLOSE};

void elevator_elevatord_connected(struct ubus_context* ctx, uint32_t id) {
    _ctx = ctx;
    _elevatord_object_id = id;
}

void elevator_elevatord_disconnected(struct ubus_context* ctx) {
    (void)ctx;
    _ctx = NULL;
    _elevatord_object_id = 0;
}

int elevator_property_elevator_id(struct property* self) {
    (void)self;
    if (!self) return -1;

    property_value_set_string_ext(&self->value, _elevator_id, 1);

    // self->dirty = 1;

    return 0;
}

static int _elevatord_enter_sensor_calibration(void* args) {
    (void)args;
    struct property* prop = (struct property*)args;
    struct blob_buf b;
    if (!_ctx || _elevatord_object_id == 0 || !prop) {
        return -1;
    }

    HR_LOGD("sensor prop:%s\n", prop->name);
    memset((void*)&b, 0, sizeof(b));
    blob_buf_init(&b, 0);
    int ret = ubus_invoke(_ctx, _elevatord_object_id, "enter_sensor_calibration", b.head, NULL, NULL, 0);
    HR_LOGD("%s(%d):   ret :%d\n", __FUNCTION__, __LINE__, ret);
    blob_buf_free(&b);

    return 0;
}

int elevator_property_enter_sensor_calibration(struct property* self, struct property_value* value) {
    if (!self || !value || !_ctx || _elevatord_object_id == 0) {
        return -1;
    }
    return post_async_task(_elevatord_enter_sensor_calibration, self);
}

static void _on_floor_calibration_event(int id, int floor, const char* label, double height, double pressure, int completed) {
    struct floor_calibration_event* e = NULL;
    if (!label) {
        return;
    }
    HR_LOGD("%s(%d): auto calibration event:%d %d %s %f\n", __FUNCTION__, __LINE__, id, floor, label, height);

    e = floor_calibration_event_alloc();
    if (!e) {
        return;
    }
    e->id = id;
    e->floor = floor;
    e->height = height;
    e->pressure = pressure;

    if (label) {
        snprintf(e->label, sizeof(e->label), "%s", label);
    } else {
        snprintf(e->label, sizeof(e->label), "%d", floor);
    }

    send_floor_calibration_event(e);

    if (completed != 0) {
        // report floor model data

        properties_tbl[PROPERTY_FLOOR_MODEL].dirty = 1;
        topic_property_report();
    }
}

static int _elevatord_floor_enter_calibration(void* args) {
    (void)args;
    int* p = (int*)args;
    struct blob_buf b;
    if (!_ctx || _elevatord_object_id == 0) {
        return -1;
    }

    memset((void*)&b, 0, sizeof(b));
    blob_buf_init(&b, 0);
    blobmsg_add_u32(&b, "BaseFloor", p[0]);
    blobmsg_add_u32(&b, "FloorsBelow", p[1]);
    blobmsg_add_u32(&b, "FloorsAbove", p[2]);
    int ret = ubus_invoke(_ctx, _elevatord_object_id, "StartAutoFloorCalibration", b.head, NULL, NULL, 0);
    HR_LOGD("%s(%d):   ret :%d\n", __FUNCTION__, __LINE__, ret);
    blob_buf_free(&b);

    free(p);

    return 0;
}

int elevator_floor_enter_calibration(int floor_base, int floors_below_base, int floors_above_base) {
    int* p = (int*)calloc(1, sizeof(int) * 3);
    if (!_ctx || _elevatord_object_id == 0) {
        return -1;
    }

    p[0] = floor_base;
    p[1] = floors_below_base;
    p[2] = floors_above_base;
    return post_async_task(_elevatord_floor_enter_calibration, p);
}

void elevator_ubus_event_handler(struct ubus_context* ctx,
                                 struct ubus_event_handler* ev,
                                 const char* type,
                                 struct blob_attr* msg) {
    (void)ctx;
    (void)ev;
    (void)type;
    (void)msg;

    char* str = blobmsg_format_json(msg, true);
    HR_LOGD("%s(%d) %s: %s\n", __FUNCTION__, __LINE__, type, str);
    free(str);

    if (strncmp(type, ELEVATOR_EVENT_PREFIX, strlen(ELEVATOR_EVENT_PREFIX)) == 0) {
        const char* event = type + strlen(ELEVATOR_EVENT_PREFIX);
        // HR_LOGD("%s(%d): type:%s -> %s\n", __FUNCTION__, __LINE__, type, event);
        // door
        // person
        // ebike

        if (0 == strcmp("door", event)) {
            struct blob_attr* tb[2] = {NULL};
            static const struct blobmsg_policy policy[] = {
                {.name = "status", .type = BLOBMSG_TYPE_STRING},
                {NULL, BLOBMSG_TYPE_UNSPEC},
            };

            blobmsg_parse(policy, sizeof(policy) / sizeof(policy[0]), tb, blobmsg_data(msg),
                          blobmsg_data_len(msg));

            if (!tb[0]) {
                return;
            }

            const char* status = blobmsg_get_string(tb[0]);
            if (!status) {
                return;
            }
            if (0 == strcmp("open", status)) {
                _status.door_state = ELEVATOR_DOOR_OPEN;
            } else if (0 == strcmp("close", status)) {
                _status.door_state = ELEVATOR_DOOR_CLOSE;
            }
        } else if (0 == strcmp("person", event)) {
            int num = 0;
            struct blob_attr* tb[2] = {NULL};
            static const struct blobmsg_policy policy[] = {
                {.name = "num", .type = BLOBMSG_TYPE_INT32},
                {NULL, BLOBMSG_TYPE_UNSPEC},
            };

            blobmsg_parse(policy, sizeof(policy) / sizeof(policy[0]), tb, blobmsg_data(msg),
                          blobmsg_data_len(msg));

            if (!tb[0]) {
                return;
            }

            num = blobmsg_get_u32(tb[0]);
            _status.passenger_count = num;
        } else if (0 == strcmp("ebike", event)) {
            struct blob_attr* tb[2] = {NULL};
            static const struct blobmsg_policy policy[] = {
                {.name = "status", .type = BLOBMSG_TYPE_INT32},
                {NULL, BLOBMSG_TYPE_UNSPEC},
            };

            blobmsg_parse(policy, sizeof(policy) / sizeof(policy[0]), tb, blobmsg_data(msg),
                          blobmsg_data_len(msg));

            if (!tb[0]) {
                return;
            }

            if (blobmsg_get_u32(tb[0]) == 1) {
                HR_LOGD("receive ebike fire event!\n");
            } else {
                HR_LOGD("receive ebike cancel event!\n");
            }
        }
    }
}

int elevator_elevatord_subscriber_callback(struct ubus_context* ctx, struct ubus_object* obj, struct ubus_request_data* req, const char* method, struct blob_attr* msg) {
    (void)ctx;
    (void)obj;
    (void)req;
    (void)method;
    (void)msg;

    int calibration = 0;
    double bias_accel_x = 0;
    double bias_accel_y = 0;
    double bias_accel_z = 0;
    double bias_pitch = 0;
    double bias_roll = 0;

    if (!method) {
        return -1;
    }

    HR_LOGD("%s(%d): %s.............\n", __FUNCTION__, __LINE__, method);
    char* str = blobmsg_format_json(msg, true);
    HR_LOGE("%s(%d): str:%s\n", __FUNCTION__, __LINE__, str);
    free(str);

    if (0 == strcmp(ELEVATORD_EVENT_SENSOR_CALIBRATION, method)) {
        struct blob_attr* tb[7] = {NULL};
        static const struct blobmsg_policy policy[] = {
            {.name = "calibration", .type = BLOBMSG_TYPE_INT32},
            {.name = "bias_accel_x", .type = BLOBMSG_TYPE_DOUBLE},
            {.name = "bias_accel_y", .type = BLOBMSG_TYPE_DOUBLE},
            {.name = "bias_accel_z", .type = BLOBMSG_TYPE_DOUBLE},
            {.name = "bias_pitch", .type = BLOBMSG_TYPE_DOUBLE},
            {.name = "bias_roll", .type = BLOBMSG_TYPE_DOUBLE},
            {NULL, BLOBMSG_TYPE_UNSPEC},
        };

        blobmsg_parse(policy, sizeof(policy) / sizeof(policy[0]), tb, blobmsg_data(msg),
                      blobmsg_data_len(msg));

        if (!tb[0] || !tb[1] || !tb[2] || !tb[3] || !tb[4] || !tb[5]) {
            return -1;
        }

        calibration = blobmsg_get_u32(tb[0]);

        bias_accel_x = blobmsg_get_double(tb[1]);
        bias_accel_y = blobmsg_get_double(tb[2]);
        bias_accel_z = blobmsg_get_double(tb[3]);
        bias_pitch = blobmsg_get_double(tb[4]);
        bias_roll = blobmsg_get_double(tb[5]);

        property_value_set_number(&properties_tbl[PROPERTY_IMU_CALIBRATION].value, calibration);
        properties_tbl[PROPERTY_IMU_CALIBRATION].dirty = 1;

        property_value_set_decimal(&properties_tbl[PROPERTY_BIAS_ACCEL_X].value, bias_accel_x);
        properties_tbl[PROPERTY_BIAS_ACCEL_X].dirty = 1;

        property_value_set_decimal(&properties_tbl[PROPERTY_BIAS_ACCEL_Y].value, bias_accel_y);
        properties_tbl[PROPERTY_BIAS_ACCEL_Y].dirty = 1;

        property_value_set_decimal(&properties_tbl[PROPERTY_BIAS_ACCEL_Z].value, bias_accel_z);
        properties_tbl[PROPERTY_BIAS_ACCEL_Z].dirty = 1;

        property_value_set_decimal(&properties_tbl[PROPERTY_BIAS_PITCH].value, bias_pitch);
        properties_tbl[PROPERTY_BIAS_PITCH].dirty = 1;

        property_value_set_decimal(&properties_tbl[PROPERTY_BIAS_ROLL].value, bias_roll);
        properties_tbl[PROPERTY_BIAS_ROLL].dirty = 1;

        // schedule report
        topic_property_report();

    } else if (0 == strcmp(ELEVATORD_EVENT_AUTOFLOORCALIBRATIONEVENT, method)) {
        // {"Id":0,"Floor":-1,"Label":"-1","Height":4.646000,"Pressure":96970.700000}

        struct blob_attr* tb[7] = {NULL};
        static const struct blobmsg_policy policy[] = {
            {.name = "Id", .type = BLOBMSG_TYPE_INT32},
            {.name = "Floor", .type = BLOBMSG_TYPE_INT32},
            {.name = "Label", .type = BLOBMSG_TYPE_STRING},
            {.name = "Height", .type = BLOBMSG_TYPE_DOUBLE},
            {.name = "Pressure", .type = BLOBMSG_TYPE_DOUBLE},
            {.name = "Completed", .type = BLOBMSG_TYPE_INT32},
            {NULL, BLOBMSG_TYPE_UNSPEC},
        };

        blobmsg_parse(policy, sizeof(policy) / sizeof(policy[0]), tb, blobmsg_data(msg),
                      blobmsg_data_len(msg));
        if (!tb[0] || !tb[1] || !tb[2] || !tb[3] || !tb[4]) {
            return -1;
        }
        _on_floor_calibration_event(blobmsg_get_u32(tb[0]),
                                    blobmsg_get_u32(tb[1]),
                                    blobmsg_get_string(tb[2]),
                                    blobmsg_get_double(tb[3]),
                                    blobmsg_get_double(tb[4]),
                                    blobmsg_get_u32(tb[5]));
    }

    return 0;
}
