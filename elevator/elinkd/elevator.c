
// mxp, 20250711, implement elevator function in elinkd

#include "elevator.h"

#include <json-c/json_tokener.h>
#include <stdint.h>

#include "elinkd.h"
#include "file_util.h"
#include "hr_log.h"
#include "libubox/blob.h"
#include "libubox/blobmsg.h"
#include "libubox/blobmsg_json.h"
#include "libubus.h"
#include "platform.h"
#include "property.h"
#include "sconf.h"
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

enum {
    OPTION_EGUARD_ALARM_SWITCH = 0,
    OPTION_EGUARD_ALARM_INTERVAL,
    OPTION_EGUARD_ALARM_REPEAT_COUNT,
    OPTION_EGUARD_ALARM_EBIKE_REPEAT_COUNT,
    OPTION_EGUARD_ALARM_KUNREN_REPEAT_COUNT,
    OPTION_EGUARD_DTOF_SWITCH,
    OPTION_EGUARD_DTOF_OCCLUSION_DISTANCE,
    OPTION_EGUARD_KUNREN_DETECT_ENABLED,
    OPTION_EGUARD_KUNREN_DETECT_TIMEOUT,
    OPTION_EGUARD_DOOR_ZONE_STOPPED_THRESHOLD,
    OPTION_EGUARD_DOOR_CONTROL_ENABLED,
};

static struct sconf_proto _elevatord_options[] = {
    [OPTION_EGUARD_ALARM_SWITCH] = {"EGUARD_ALARM_SWITCH", PROTO_VALUE_NUMBER, {.number = 1}},
    [OPTION_EGUARD_ALARM_INTERVAL] = {"EGUARD_ALARM_INTERVAL", PROTO_VALUE_NUMBER, {.number = 3000}},  // 3s
    [OPTION_EGUARD_ALARM_REPEAT_COUNT] = {"EGUARD_ALARM_REPEAT_COUNT", PROTO_VALUE_NUMBER, {.number = 3}},
    [OPTION_EGUARD_ALARM_EBIKE_REPEAT_COUNT] = {"EGUARD_ALARM_EBIKE_REPEAT_COUNT", PROTO_VALUE_NUMBER, {.number = INT64_MIN}},  // not set, use global
    // mxp, 20250826, default kunren count is 0, and not use default global alarm repeat count
    [OPTION_EGUARD_ALARM_KUNREN_REPEAT_COUNT] = {"EGUARD_ALARM_KUNREN_REPEAT_COUNT", PROTO_VALUE_NUMBER, {.number = 0}},
    [OPTION_EGUARD_DTOF_SWITCH] = {"EGUARD_DTOF_SWITCH", PROTO_VALUE_NUMBER, {.number = 1}},
    [OPTION_EGUARD_DTOF_OCCLUSION_DISTANCE] = {"EGUARD_DTOF_OCCLUSION_DISTANCE", PROTO_VALUE_NUMBER, {.number = 100}},  // 100mm
    [OPTION_EGUARD_KUNREN_DETECT_ENABLED] = {"EGUARD_KUNREN_DETECT_ENABLED", PROTO_VALUE_NUMBER, {.number = 1}},
    [OPTION_EGUARD_KUNREN_DETECT_TIMEOUT] = {"EGUARD_KUNREN_DETECT_TIMEOUT", PROTO_VALUE_NUMBER, {.number = 90000}},            // 90s
    [OPTION_EGUARD_DOOR_ZONE_STOPPED_THRESHOLD] = {"EGUARD_DOOR_ZONE_STOPPED_THRESHOLD", PROTO_VALUE_DECIMAL, {.decimal = 0}},  // not enable
    [OPTION_EGUARD_DOOR_CONTROL_ENABLED] = {"EGUARD_DOOR_CONTROL_ENABLED", PROTO_VALUE_NUMBER, {.number = 0}},
};

// static void read_elevator_options() {
//     // no string(dynamic memory), so we can directly reload
//     sconf_load_with_proto(EGUARD_CONFIG_PATH, _elevatord_options, sizeof(_elevatord_options) / sizeof(_elevatord_options[0]));
// }

static void restart_service(const char* svc) {
    if (!svc) return;
    pid_t pid = fork();
    if (pid == 0) {
        char path[64] = {0};
        snprintf(path, sizeof(path), "/etc/init.d/%s", svc);
        execl(path, svc, "restart", NULL);
        _exit(1);
    }
}

void elevator_elevatord_connected(struct ubus_context* ctx, uint32_t id) {
    _ctx = ctx;
    _elevatord_object_id = id;
}

void elevator_elevatord_disconnected(struct ubus_context* ctx) {
    (void)ctx;
    _ctx = NULL;
    _elevatord_object_id = 0;
}

int elevator_property_get_elevator_id(struct property* self) {
    (void)self;
    if (!self) return -1;

    platform_get_property(PROPERTY_DEVICEID, _elevator_id, sizeof(_elevator_id));

    // convert to empty string when it's "0"
    if (_elevator_id[0] == '0' && _elevator_id[1] == '\0') {
        _elevator_id[0] = '\0';
    }

    property_value_set_string_ext(&self->value, _elevator_id, 1);

    return 0;
}

int elevator_property_set_elevator_id(struct property* self, struct property_value* value) {
    if (!self || !value) {
        return -1;
    }

    if (value->type != E_STRING || !value->val.string) {
        return -1;
    }

    // convert to "0" when it's empty, unifykeys not support empty string
    if (strlen(value->val.string) == 0) {
        printf("xxxxxxxxxxxxxxxx\n");
        snprintf(_elevator_id, sizeof(_elevator_id), "%d", 0);
    } else {
        printf("xxxxxxxxxxxxxxxx yyyyyyyyyyy\n");
        snprintf(_elevator_id, sizeof(_elevator_id), "%s", value->val.string);
    }

    printf("now elevator id:%s\n", _elevator_id);
    platform_set_property(PROPERTY_DEVICEID, _elevator_id);
    properties_tbl[PROPERTY_ELEVATOR_ID].dirty = 1;

    // we should restart hqliftd immediately
    // system("/etc/init.d/hqliftd restart > /dev/null 2>&1");
    restart_service("hqliftd");

    return 0;
}

static int _elevatord_enter_sensor_calibration(void* args) {
    (void)args;
    struct property* prop = (struct property*)args;
    struct blob_buf b;
    if (!_ctx || _elevatord_object_id == 0 || !prop) {
        return -1;
    }

    memset((void*)&b, 0, sizeof(b));
    blob_buf_init(&b, 0);
    ubus_invoke(_ctx, _elevatord_object_id, "enter_sensor_calibration", b.head, NULL, NULL, 0);
    blob_buf_free(&b);

    return 0;
}

int elevator_property_enter_sensor_calibration(struct property* self, struct property_value* value) {
    if (!self || !value || !_ctx || _elevatord_object_id == 0) {
        return -1;
    }
    return post_async_task(_elevatord_enter_sensor_calibration, self);
}

int elevator_property_get_floor_model(struct property* self) {
    if (!self) {
        return -1;
    }
    char* data = NULL;
    futil_read(ELEVATORD_FLOOR_MODEL_PATH, &data);
    if (!data) {
        property_value_set_string_ext(&self->value, "", 1);
        return 0;
    }

    property_value_set_string(&self->value, data);
    free(data);

    return 0;
}

int elevator_property_get_hqliftd_config(struct property* self) {
    char* data = NULL;
    futil_read(HQLIFTD_CONFIG_PATH, &data);
    if (!data) {
        property_value_set_string_ext(&self->value, "", 1);
        return 0;
    }
    property_value_set_string(&self->value, data);
    free(data);
    return 0;
}

int elevator_property_set_hqliftd_config(struct property* self, struct property_value* value) {
    if (!self || !value) {
        return -1;
    }

    if (value->type != E_STRING || !value->val.string) {
        return -1;
    }

    futil_write(HQLIFTD_CONFIG_PATH, (void*)value->val.string, strlen(value->val.string));
    properties_tbl[PROPERTY_HQLIFTD_CONFIG].dirty = 1;

    // hqliftd can auto reload but some parameter also take effect only after restart
    // such as lift realtime report period: REALTIME_REPORT_PERIOD_MS
    // system("/etc/init.d/hqliftd restart > /dev/null 2>&1");
    restart_service("hqliftd");

    return 0;
}

int elevator_property_get_eguard_alarm_switch(struct property* self) {
    if (!self) {
        return -1;
    }

    sconf_load_with_proto(EGUARD_CONFIG_PATH, &_elevatord_options[OPTION_EGUARD_ALARM_SWITCH], 1);

    property_value_set_number(&self->value, _elevatord_options[OPTION_EGUARD_ALARM_SWITCH].value.number);
    return 0;
}

int elevator_property_set_eguard_alarm_switch(struct property* self, struct property_value* value) {
    if (!self || !value || value->type != E_NUMBER) {
        return -1;
    }

    _elevatord_options[OPTION_EGUARD_ALARM_SWITCH].value.number = !!value->val.number;
    sconf_save_with_proto(EGUARD_CONFIG_PATH, &_elevatord_options[OPTION_EGUARD_ALARM_SWITCH], 1);

    properties_tbl[PROPERTY_EGUARD_ALARM_SWITCH].dirty = 1;

    // system("/etc/init.d/eguard restart > /dev/null 2>&1");
    // eguard auto reload
    // restart_service("eguard");
    return 0;
}

int elevator_property_get_eguard_alarm_interval(struct property* self) {
    if (!self) {
        return -1;
    }

    sconf_load_with_proto(EGUARD_CONFIG_PATH, &_elevatord_options[OPTION_EGUARD_ALARM_INTERVAL], 1);

    property_value_set_number(&self->value, _elevatord_options[OPTION_EGUARD_ALARM_INTERVAL].value.number);
    return 0;
}

int elevator_property_set_eguard_alarm_interval(struct property* self, struct property_value* value) {
    if (!self || !value || value->type != E_NUMBER) {
        return -1;
    }

    // do not allow too small
    if (value->val.number < 1000) {
        HR_LOGD("invalid eguard_alarm_interval:%ld < 1000ms\n", value->val.number);
        return -1;
    }

    _elevatord_options[OPTION_EGUARD_ALARM_INTERVAL].value.number = (int)value->val.number;
    sconf_save_with_proto(EGUARD_CONFIG_PATH, &_elevatord_options[OPTION_EGUARD_ALARM_INTERVAL], 1);

    properties_tbl[PROPERTY_EGUARD_ALARM_INTERVAL].dirty = 1;

    // system("/etc/init.d/eguard restart > /dev/null 2>&1");
    // eguard auto reload
    // restart_service("eguard");

    return 0;
}

int elevator_property_get_eguard_alarm_repeat_count(struct property* self) {
    if (!self) {
        return -1;
    }

    sconf_load_with_proto(EGUARD_CONFIG_PATH, &_elevatord_options[OPTION_EGUARD_ALARM_REPEAT_COUNT], 1);

    property_value_set_number(&self->value, _elevatord_options[OPTION_EGUARD_ALARM_REPEAT_COUNT].value.number);
    return 0;
}

int elevator_property_set_eguard_alarm_repeat_count(struct property* self, struct property_value* value) {
    if (!self || !value || value->type != E_NUMBER) {
        return -1;
    }

    if (value->val.number < -1) {
        HR_LOGE("invalid eguard_alarm_repeat_count:%ld < -1, accept: -1/0/>0\n", value->val.number);
        return -1;
    }

    _elevatord_options[OPTION_EGUARD_ALARM_REPEAT_COUNT].value.number = (int)value->val.number;
    sconf_save_with_proto(EGUARD_CONFIG_PATH, &_elevatord_options[OPTION_EGUARD_ALARM_REPEAT_COUNT], 1);

    properties_tbl[PROPERTY_EGUARD_ALARM_REPEAT_COUNT].dirty = 1;

    // system("/etc/init.d/eguard restart > /dev/null 2>&1");
    // eguard auto reload
    // restart_service("eguard");

    return 0;
}

int elevator_property_get_eguard_alarm_ebike_repeat_count(struct property* self) {
    if (!self) {
        return -1;
    }

    sconf_load_with_proto(EGUARD_CONFIG_PATH, &_elevatord_options[OPTION_EGUARD_ALARM_EBIKE_REPEAT_COUNT], 1);
    // not set, use global
    if (_elevatord_options[OPTION_EGUARD_ALARM_EBIKE_REPEAT_COUNT].value.number == INT64_MIN) {
        sconf_load_with_proto(EGUARD_CONFIG_PATH, &_elevatord_options[OPTION_EGUARD_ALARM_REPEAT_COUNT], 1);
        property_value_set_number(&self->value, _elevatord_options[OPTION_EGUARD_ALARM_REPEAT_COUNT].value.number);
    } else {
        property_value_set_number(&self->value, _elevatord_options[OPTION_EGUARD_ALARM_EBIKE_REPEAT_COUNT].value.number);
    }

    return 0;
}

int elevator_property_set_eguard_alarm_ebike_repeat_count(struct property* self, struct property_value* value) {
    if (!self || !value || value->type != E_NUMBER) {
        return -1;
    }

    if (value->val.number < -1) {
        HR_LOGE("invalid eguard_alarm_ebike_repeat_count:%ld < -1, accept: -1/0/>0\n", value->val.number);
        return -1;
    }

    _elevatord_options[OPTION_EGUARD_ALARM_EBIKE_REPEAT_COUNT].value.number = (int)value->val.number;
    sconf_save_with_proto(EGUARD_CONFIG_PATH, &_elevatord_options[OPTION_EGUARD_ALARM_EBIKE_REPEAT_COUNT], 1);

    properties_tbl[PROPERTY_EGUARD_ALARM_EBIKE_REPEAT_COUNT].dirty = 1;

    // system("/etc/init.d/eguard restart > /dev/null 2>&1");

    // eguard auto reload
    // restart_service("eguard");

    return 0;
}

int elevator_property_get_eguard_alarm_kunren_repeat_count(struct property* self) {
    if (!self) {
        return -1;
    }

    sconf_load_with_proto(EGUARD_CONFIG_PATH, &_elevatord_options[OPTION_EGUARD_ALARM_KUNREN_REPEAT_COUNT], 1);

    // not set, use global
    // if (_elevatord_options[OPTION_EGUARD_ALARM_KUNREN_REPEAT_COUNT].value.number == INT64_MIN) {
    //    property_value_set_number(&self->value, _elevatord_options[OPTION_EGUARD_ALARM_REPEAT_COUNT].value.number);
    // } else {
        property_value_set_number(&self->value, _elevatord_options[OPTION_EGUARD_ALARM_KUNREN_REPEAT_COUNT].value.number);
    // }

    return 0;
}

int elevator_property_set_eguard_alarm_kunren_repeat_count(struct property* self, struct property_value* value) {
    if (!self || !value || value->type != E_NUMBER) {
        return -1;
    }

    if (value->val.number < -1) {
        HR_LOGE("invalid eguard_alarm_kunren_repeat_count:%ld < -1, accept: -1/0/>0\n", value->val.number);
        return -1;
    }

    _elevatord_options[OPTION_EGUARD_ALARM_KUNREN_REPEAT_COUNT].value.number = (int)value->val.number;
    sconf_save_with_proto(EGUARD_CONFIG_PATH, &_elevatord_options[OPTION_EGUARD_ALARM_KUNREN_REPEAT_COUNT], 1);

    properties_tbl[PROPERTY_EGUARD_ALARM_KUNREN_REPEAT_COUNT].dirty = 1;

    return 0;
}

int elevator_property_get_eguard_dtof_switch(struct property* self) {
    if (!self) {
        return -1;
    }

    sconf_load_with_proto(EGUARD_CONFIG_PATH, &_elevatord_options[OPTION_EGUARD_DTOF_SWITCH], 1);

    property_value_set_number(&self->value, _elevatord_options[OPTION_EGUARD_DTOF_SWITCH].value.number);
    return 0;
}

int elevator_property_set_eguard_dtof_switch(struct property* self, struct property_value* value) {
    if (!self || !value || value->type != E_NUMBER) {
        return -1;
    }

    _elevatord_options[OPTION_EGUARD_DTOF_SWITCH].value.number = !!value->val.number;
    sconf_save_with_proto(EGUARD_CONFIG_PATH, &_elevatord_options[OPTION_EGUARD_DTOF_SWITCH], 1);

    properties_tbl[PROPERTY_EGUARD_DTOF_SWITCH].dirty = 1;

    // system("/etc/init.d/eguard restart > /dev/null 2>&1");

    // eguard auto reload
    // restart_service("eguard");
    return 0;
}

int elevator_property_get_eguard_dtof_occlusion_distance(struct property* self) {
    if (!self) {
        return -1;
    }

    sconf_load_with_proto(EGUARD_CONFIG_PATH, &_elevatord_options[OPTION_EGUARD_DTOF_OCCLUSION_DISTANCE], 1);

    property_value_set_number(&self->value, _elevatord_options[OPTION_EGUARD_DTOF_OCCLUSION_DISTANCE].value.number);
    return 0;
}

int elevator_property_set_eguard_dtof_occlusion_distance(struct property* self, struct property_value* value) {
    if (!self || !value || value->type != E_NUMBER) {
        return -1;
    }

    if (value->val.number < 0) {
        HR_LOGE("invalid eguard_dtof_occlusion_distance:%ld < 0\n", value->val.number);
        return -1;
    }

    _elevatord_options[OPTION_EGUARD_DTOF_OCCLUSION_DISTANCE].value.number = (int)value->val.number;
    sconf_save_with_proto(EGUARD_CONFIG_PATH, &_elevatord_options[OPTION_EGUARD_DTOF_OCCLUSION_DISTANCE], 1);

    properties_tbl[PROPERTY_EGUARD_DTOF_OCCLUSION_DISTANCE].dirty = 1;

    // eguard auto reload
    // restart_service("eguard");

    return 0;
}

int elevator_property_get_eguard_kunren_detect_enabled(struct property* self) {
    if (!self) {
        return -1;
    }

    sconf_load_with_proto(EGUARD_CONFIG_PATH, &_elevatord_options[OPTION_EGUARD_KUNREN_DETECT_ENABLED], 1);

    property_value_set_number(&self->value, _elevatord_options[OPTION_EGUARD_KUNREN_DETECT_ENABLED].value.number);
    return 0;
}

int elevator_property_set_eguard_kunren_detect_enabled(struct property* self, struct property_value* value) {
    if (!self || !value || value->type != E_NUMBER) {
        return -1;
    }

    _elevatord_options[OPTION_EGUARD_KUNREN_DETECT_ENABLED].value.number = !!value->val.number;
    sconf_save_with_proto(EGUARD_CONFIG_PATH, &_elevatord_options[OPTION_EGUARD_KUNREN_DETECT_ENABLED], 1);

    properties_tbl[PROPERTY_EGUARD_KUNREN_DETECT_ENABLED].dirty = 1;

    // no need restart hqliftd, it will auto reload
    // system("/etc/init.d/hqliftd restart > /dev/null 2>&1");
    // restart_service("hqliftd");

    return 0;
}

int elevator_property_get_eguard_kunren_detect_timeout(struct property* self) {
    if (!self) {
        return -1;
    }

    sconf_load_with_proto(EGUARD_CONFIG_PATH, &_elevatord_options[OPTION_EGUARD_KUNREN_DETECT_TIMEOUT], 1);

    property_value_set_number(&self->value, _elevatord_options[OPTION_EGUARD_KUNREN_DETECT_TIMEOUT].value.number);
    return 0;
}

int elevator_property_set_eguard_kunren_detect_timeout(struct property* self, struct property_value* value) {
    if (!self || !value || value->type != E_NUMBER) {
        return -1;
    }

    // kunren detect should > 30s, default 90s
    if (value->val.number < 30000) {
        HR_LOGE("invalid eguard_kunren_detect_timeout:%ld < 30s\n", value->val.number);
        return -1;
    }

    _elevatord_options[OPTION_EGUARD_KUNREN_DETECT_TIMEOUT].value.number = (int)value->val.number;
    sconf_save_with_proto(EGUARD_CONFIG_PATH, &_elevatord_options[OPTION_EGUARD_KUNREN_DETECT_TIMEOUT], 1);

    properties_tbl[PROPERTY_EGUARD_KUNREN_DETECT_TIMEOUT].dirty = 1;

    // no need restart hqliftd, it will auto reload
    // system("/etc/init.d/hqliftd restart > /dev/null 2>&1");
    // restart_service("hqliftd");

    return 0;
}

int elevator_property_get_eguard_door_zone_stopped_threshold(struct property* self) {
    if (!self) {
        return -1;
    }

    sconf_load_with_proto(EGUARD_CONFIG_PATH, &_elevatord_options[OPTION_EGUARD_DOOR_ZONE_STOPPED_THRESHOLD], 1);

    property_value_set_decimal(&self->value, _elevatord_options[OPTION_EGUARD_DOOR_ZONE_STOPPED_THRESHOLD].value.decimal);

    return 0;
}

int elevator_property_set_eguard_door_zone_stopped_threshold(struct property* self, struct property_value* value) {
    if (!self || !value || value->type != E_DECIMAL) {
        return -1;
    }

    if (value->val.decimal < 0) {
        HR_LOGD("invalid eguard_door_zone_stopped_threshold:%f\n", value->val.decimal);
        return -1;
    }
    _elevatord_options[OPTION_EGUARD_DOOR_ZONE_STOPPED_THRESHOLD].value.decimal = value->val.decimal;
    sconf_save_with_proto(EGUARD_CONFIG_PATH, &_elevatord_options[OPTION_EGUARD_DOOR_ZONE_STOPPED_THRESHOLD], 1);

    properties_tbl[PROPERTY_EGUARD_DOOR_ZONE_STOPPED_THRESHOLD].dirty = 1;

    // no need restart hqliftd, it will auto reload
    // system("/etc/init.d/hqliftd restart > /dev/null 2>&1");
    // restart_service("hqliftd");

    return 0;
}

int elevator_property_get_eguard_door_control_enabled(struct property* self) {
    if (!self) return -1;

    sconf_load_with_proto(EGUARD_CONFIG_PATH, &_elevatord_options[OPTION_EGUARD_DOOR_CONTROL_ENABLED], 1);

    property_value_set_number(&self->value, _elevatord_options[OPTION_EGUARD_DOOR_CONTROL_ENABLED].value.number);

    return 0;
}

int elevator_property_set_eguard_door_control_enabled(struct property* self, struct property_value* value) {
    if (!self || !value || value->type != E_NUMBER) {
        return -1;
    }

    _elevatord_options[OPTION_EGUARD_DOOR_CONTROL_ENABLED].value.number = !!value->val.number;
    sconf_save_with_proto(EGUARD_CONFIG_PATH, &_elevatord_options[OPTION_EGUARD_DOOR_CONTROL_ENABLED], 1);

    properties_tbl[PROPERTY_EGUARD_DOOR_CONTROL_ENABLED].dirty = 1;

    // should restart to release control when you disable it,
    // system("/etc/init.d/eguard restart > /dev/null 2>&1");
    // eguard auto reload
    // restart_service("eguard");

    return 0;
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
    if (!p) {
        return -1;
    }
    if (!_ctx || _elevatord_object_id == 0) {
        free(p);
        return -1;
    }

    memset((void*)&b, 0, sizeof(b));
    blob_buf_init(&b, 0);
    blobmsg_add_u32(&b, "BaseFloor", p[0]);
    blobmsg_add_u32(&b, "FloorsBelow", p[1]);
    blobmsg_add_u32(&b, "FloorsAbove", p[2]);
    ubus_invoke(_ctx, _elevatord_object_id, "StartAutoFloorCalibration", b.head, NULL, NULL, 0);
    blob_buf_free(&b);

    free(p);

    return 0;
}

int elevator_floor_enter_calibration(int floor_base, int floors_below_base, int floors_above_base) {
    int* p = NULL;
    if (!_ctx || _elevatord_object_id == 0) {
        return -1;
    }
    p = (int*)calloc(1, sizeof(int) * 3);
    if (!p) return -1;

    p[0] = floor_base;
    p[1] = floors_below_base;
    p[2] = floors_above_base;
    return post_async_task(_elevatord_floor_enter_calibration, p);
}

static int _elevatord_floor_calibrate_at_floor(void* args) {
    (void)args;
    int floor = (int)(intptr_t)args;
    struct blob_buf b;

    if (!_ctx || _elevatord_object_id == 0) {
        return -1;
    }

    memset((void*)&b, 0, sizeof(b));
    blob_buf_init(&b, 0);
    blobmsg_add_u32(&b, "Floor", floor);
    ubus_invoke(_ctx, _elevatord_object_id, "CalibrateAtFloorManually", b.head, NULL, NULL, 0);
    blob_buf_free(&b);

    return 0;
}

int elevator_floor_calibrate_at_floor(int floor) {
    // cast int to pointer is safe
    return post_async_task(_elevatord_floor_calibrate_at_floor, (void*)(intptr_t)floor);
}

static int _elevatord_floor_calibrate_at_height(void* args) {
    (void)args;
    double* height = (double*)args;
    struct blob_buf b;
    if (!height) {
        return -1;
    }
    if (!_ctx || _elevatord_object_id == 0) {
        return -1;
    }

    memset((void*)&b, 0, sizeof(b));
    blob_buf_init(&b, 0);
    blobmsg_add_double(&b, "Height", *height);
    ubus_invoke(_ctx, _elevatord_object_id, "CalibrateAtHeightManually", b.head, NULL, NULL, 0);
    blob_buf_free(&b);

    return 0;
}

int elevator_floor_calibrate_at_height(double height) {
    // use static memory, do not support multi call
    static int _height = 0;
    _height = height;
    return post_async_task(_elevatord_floor_calibrate_at_height, (void*)&_height);
}
static int _elevatord_floor_update_floor_model_data(void* args) {
    struct blob_buf b;
    const char* data = (const char*)args;
    if (!data) {
        return -1;
    }
    if (!_ctx || _elevatord_object_id == 0) {
        free((void*)data);
        return -1;
    }

    HR_LOGD("model:%s\n", data);
    memset((void*)&b, 0, sizeof(b));
    blob_buf_init(&b, 0);

    json_tokener* tok = json_tokener_new();
    struct json_object* root = json_tokener_parse_ex(tok, data, strlen(data));
    blobmsg_add_object(&b, root);

    ubus_invoke(_ctx, _elevatord_object_id, "update_floor_model_data", b.head, NULL, NULL, 0);
    blob_buf_free(&b);

    json_object_put(root);
    json_tokener_free(tok);

    // free memory
    free((void*)data);
    data = NULL;

    return 0;
}
int elevator_floor_update_floor_model_data(const char* data) {
    const char* model = NULL;
    if (!data) {
        return -1;
    }

    // freed in task
    model = strdup(data);

    if (!model) {
        return -1;
    }

    return post_async_task(_elevatord_floor_update_floor_model_data, (void*)model);
}
void elevator_ubus_event_handler(struct ubus_context* ctx,
                                 struct ubus_event_handler* ev,
                                 const char* type,
                                 struct blob_attr* msg) {
    (void)ctx;
    (void)ev;
    (void)type;
    (void)msg;

    // char* str = blobmsg_format_json(msg, true);
    // HR_LOGD("%s(%d) %s: %s\n", __FUNCTION__, __LINE__, type, str);
    // free(str);

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

    // char* str = blobmsg_format_json(msg, true);
    // HR_LOGE("elevatord event => %s:%s\n", method, str ? str : "");
    // free(str);

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
