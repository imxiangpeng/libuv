
#include <fcntl.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "cjson/cJSON.h"
#include "file_util.h"
#include "floor.h"
#include "hr_log.h"
#include "iot_topic.h"

#include "motion.h"
#include "platform.h"

#include "sconf.h"
#include "sensor.h"

#include "uviot.h"

#define ELEVATORD_RUNTIME_PARAM_REPORT_SWITCH "IOT_REPORT_SWITCH"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

enum {
    PROPERTY_TOPIC_POST = 0,
    PROPERTY_TOPIC_SET,
    PROPERTY_TOPIC_GET,
    _PROPERTY_TOPIC_MAX,
};

static struct uviot* _iot = NULL;

static char _elevator_id[128] = {0};

static struct sconf_proto report_switch_conf = {ELEVATORD_RUNTIME_PARAM_REPORT_SWITCH, PROTO_VALUE_INT64, {.int64 = 1}};

enum {
    PROPERTY_BUILD_TIMESTAMP = 0,
    PROPERTY_ELEVATOR_ID,
    PROPERTY_BIAS_ACCEL_X,
    PROPERTY_BIAS_ACCEL_Y,
    PROPERTY_BIAS_ACCEL_Z,
    PROPERTY_BIAS_PITCH,
    PROPERTY_BIAS_ROLL,
    PROPERTY_IMU_CALIBRATION,
    PROPERTY_FLOOR,
    PROPERTY_HEIGHT,
    PROPERTY_PRESSURE,
    PROPERTY_TEMPERATURE,
    PROPERTY_FLOOR_MODEL,
    PROPERTY_REPORT_SWITCH,
    PROPERTY_HQLIFTD_CONFIG,
    __PROPERTY_MAX
};

struct property {
    const char* name;
    enum property_data_type {
        P_INT64,
        P_DOUBLE,
        P_STRING
    } type;

    union {
        int64_t val_int64;
        double val_double;
        // !NULL: use preallocated memory, data should be ready now
        // NULL: the value need to be handled dynamically
        const char* val_str;  // use external memory
    } value;
    int dirty;
} _properties_tbl[__PROPERTY_MAX] = {
    [PROPERTY_BUILD_TIMESTAMP] = {"build_timestamp", P_STRING, {.val_str = ELEVATORD_BUILD_TIMESTAMP}, 1 /* report when startup*/},
    [PROPERTY_ELEVATOR_ID] = {"elevator_id", P_STRING, {.val_str = _elevator_id}, 1 /* report when startup*/},
    // calibration
    [PROPERTY_BIAS_ACCEL_X] = {"bias_accel_x", P_DOUBLE, {0}, 0},
    [PROPERTY_BIAS_ACCEL_Y] = {"bias_accel_y", P_DOUBLE, {0}, 0},
    [PROPERTY_BIAS_ACCEL_Z] = {"bias_accel_z", P_DOUBLE, {0}, 0},
    [PROPERTY_BIAS_PITCH] = {"bias_pitch", P_DOUBLE, {0}, 0},
    [PROPERTY_BIAS_ROLL] = {"bias_roll", P_DOUBLE, {0}, 0},
    [PROPERTY_IMU_CALIBRATION] = {"imu_calibration", P_INT64, {0}, 0},
    // floor
    [PROPERTY_FLOOR] = {"floor", P_INT64, {0}, 0},
    [PROPERTY_HEIGHT] = {"height", P_DOUBLE, {0}, 0},
    // do not add speed it's realtime parameter, ali display not good
    // also pressure and temperature only reported after run finished
    [PROPERTY_PRESSURE] = {"pressure", P_DOUBLE, {0}, 0},
    [PROPERTY_TEMPERATURE] = {"temperature", P_DOUBLE, {0}, 0},
    // this is special parameter, we should read data dynamic
    [PROPERTY_FLOOR_MODEL] = {"floor_model", P_STRING, {0}, 0},
    [PROPERTY_REPORT_SWITCH] = {"report_switch", P_INT64, {0}, 0},
    [PROPERTY_HQLIFTD_CONFIG] = {"hqliftd_config", P_STRING, {0}, 0},
};

static int _realtime_report_times = 0;
static const int _realtime_report_fac = 60 * 100;  // 100;  // 10 * sampling_rate = 100 * 1/100 = 1s

static void schedule_report(void);
static void _iot_motion_observer_on_sensor_calibration(struct motion_sensor_calibration_event* data);

static void _observer_on_status(struct motion_status* st);
static void _observer_on_event(struct motion_event* data);

static struct motion_observer _iot_property_observer = {
    // we use on_event only report when finished
    .on_status = _observer_on_status,
    .on_event = _observer_on_event,
    .on_sensor_calibration = _iot_motion_observer_on_sensor_calibration,
};

static int _on_publish(void** payload, int* len) {
    char tmp[256] = {0};
    cJSON *root = NULL, *param = NULL;

    root = cJSON_CreateObject();
    if (!root)
        return -1;

    // test code, please only response when needed
    snprintf(tmp, sizeof(tmp), "%d", iot_generate_mid());
    cJSON_AddStringToObject(root, "id", tmp);
    cJSON_AddStringToObject(root, "version", "1.0.0");

    param = cJSON_AddObjectToObject(root, "params");

    for (size_t i = 0; i < ARRAY_SIZE(_properties_tbl); i++) {
        struct property* prop = &_properties_tbl[i];
        // HR_LOGD("%s(%d): property:%s, type:%d, dirty:%d\n", __FUNCTION__, __LINE__, prop->name, prop->type, prop->dirty);
        if (prop->dirty == 0) {
            continue;
        }
        prop->dirty = 0;
        switch (prop->type) {
            case P_INT64:
                cJSON_AddNumberToObject(param, prop->name, prop->value.val_int64);
                break;
            case P_DOUBLE:
                cJSON_AddNumberToObject(param, prop->name, prop->value.val_double);
                break;
            case P_STRING: {
                if (prop->value.val_str) {
                    cJSON_AddStringToObject(param, prop->name, prop->value.val_str);
                    break;
                }

                // string point is null
                // the value maybe should process manually
                if (strcmp("floor_model", prop->name) == 0) {
                    char* data = NULL;
                    futil_read(floor_model_data_path(), &data);

                    cJSON_AddStringToObject(param, prop->name, data);
                    HR_LOGD("property report floor model:%s\n", data);
                    free(data);
                } else if (strcmp("hqliftd_config", prop->name) == 0) {
                    char* data = NULL;
                    futil_read(HQLIFTD_CONFIG_PATH, &data);
                    cJSON_AddStringToObject(param, prop->name, data);
                    free(data);
                }
                break;
            }
        }
    }

    *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!*payload)
        return -1;

    *len = strlen(*payload);
    HR_LOGD("publish: %s\n", (char*)*payload);

    return 0;
}

static int _on_reply_message(void* payload, int len) {
    (void)payload;
    (void)len;
    // printf("reply message %d -> %s\n", len, (char*)payload);
    return 0;
}

static size_t _popen_result(int* result, const char* cmd, char* buffer, size_t size) {
    FILE* pstream = NULL;
    size_t read_size = 0;
    if (!result || !cmd || !buffer | (size <= 0)) {
        return -1;
    }

    pstream = popen(cmd, "r");
    if (!pstream)
        return -1;

    memset((void*)buffer, 0, size);

    while (!feof(pstream) && read_size < size) {
        size_t length = fread(buffer + read_size, 1, size - read_size, pstream);
        read_size += length;
    }

    buffer[size - 1] = '\0';  // ensure null
    *result = pclose(pstream);

    *result = read_size != 0 ? 0 : -1;
    return read_size;
}

static int _on_property_set_message(void* payload, int len) {
    // printf("set message %d -> %s\n", len, (char*)payload);
    char* method = NULL;
    double val = 0;
    const char* val_str = NULL;
    cJSON *root = NULL, *params = NULL, *ele = NULL;
    if (!payload || len == 0) {
        HR_LOGE("%s(%d): invalid method ...\n", __FUNCTION__, __LINE__);
        return -1;
    }

    root = cJSON_ParseWithLength((const char*)payload, len);
    if (!root) {
        return -1;
    }

    method = cJSON_GetStringValue(cJSON_GetObjectItem(root, "method"));
    if (!method || 0 != strcmp("thing.service.property.set", method)) {
        cJSON_Delete(root);
        return -1;
    }

    params = cJSON_GetObjectItem(root, "params");
    if (!params) {
        cJSON_Delete(root);
        return -1;
    }

    cJSON_ArrayForEach(ele, params) {
        HR_LOGD("ele: %s -> type:%d\n", ele->string, ele->type);
    }

    val = cJSON_GetNumberValue(cJSON_GetObjectItem(params, "imu_calibration"));
    if (!isnan(val)) {
        HR_LOGD("%s(%d): enter calibration val: %d\n", __FUNCTION__, __LINE__, (int)val);
        if (val == 1) {
            motion_enter_sensor_calibration();
        }
    }

    val_str = cJSON_GetStringValue(cJSON_GetObjectItem(params, "elevator_id"));
    if (val_str) {
        // burn elevator id
        snprintf(_elevator_id, sizeof(_elevator_id), "%s", val_str);
        _properties_tbl[PROPERTY_ELEVATOR_ID].dirty = 1;

        platform_set_property(PROPERTY_DEVICEID, _elevator_id);

        schedule_report();

        system("/etc/init.d/S68hqliftd restart 2>&1 > /dev/null");
    }

    val = cJSON_GetNumberValue(cJSON_GetObjectItem(params, "report_switch"));
    if (!isnan(val)) {
        report_switch_conf.value.int64 = (int64_t)val;
        sconf_save_with_proto(ELEVATORD_CONFIG_PATH, &report_switch_conf, 1);

        _properties_tbl[PROPERTY_REPORT_SWITCH].value.val_int64 = report_switch_conf.value.int64;
        _properties_tbl[PROPERTY_REPORT_SWITCH].dirty = 1;
        schedule_report();
    }

    val_str = cJSON_GetStringValue(cJSON_GetObjectItem(params, "hqliftd_config"));
    if (val_str) {
        futil_write(HQLIFTD_CONFIG_PATH, (void*)val_str, strlen(val_str));
        _properties_tbl[PROPERTY_HQLIFTD_CONFIG].dirty = 1;
        schedule_report();
    }

    cJSON_Delete(root);

    return 0;
}

static int _on_property_get_message(void* payload, int len) {
    printf("get message %d -> %s\n", len, (char*)payload);
    char* method = NULL;
    //double val = 0;
    //const char* val_str = NULL;
    cJSON *root = NULL, *params = NULL, *ele = NULL;
    if (!payload || len == 0) {
        HR_LOGE("%s(%d): invalid method ...\n", __FUNCTION__, __LINE__);
        return -1;
    }

    root = cJSON_ParseWithLength((const char*)payload, len);
    if (!root) {
        return -1;
    }

    method = cJSON_GetStringValue(cJSON_GetObjectItem(root, "method"));
    if (!method || 0 != strcmp("thing.service.property.get", method)) {
        cJSON_Delete(root);
        return -1;
    }

    params = cJSON_GetObjectItem(root, "params");
    if (!params) {
        cJSON_Delete(root);
        return -1;
    }

    cJSON_ArrayForEach(ele, params) {
        HR_LOGD("ele: %s -> type:%d\n", ele->string, ele->type);
    }

    return 0;
}


static struct uviot_topic _iot_property_topics[_PROPERTY_TOPIC_MAX] = {
    [PROPERTY_TOPIC_POST] = {
        .name = "event/property/post",
        .topic = {0},
        .period = 0,
        .auto_public = 0,
        .type = TOPIC_TYPE_PUBLISH,
        .callback.on_publish = _on_publish,
    },
    /*{
        .name = "event/property/post_reply",
        .topic = {0},
        .type = TOPIC_TYPE_SUBSCRIBE,
        .callback.on_message = _on_reply_message,
    },*/
    [PROPERTY_TOPIC_SET] = {
        .name = "service/property/set",
        .topic = {0},
        .type = TOPIC_TYPE_SUBSCRIBE,
        .callback.on_message = _on_property_set_message,
    },[PROPERTY_TOPIC_GET] = {
        .name = "service/property/get",
        .topic = {0},
        .type = TOPIC_TYPE_SUBSCRIBE,
        .callback.on_message = _on_property_set_message,
    }};

int iot_topic_property_init(struct uviot* iot, const char* public_key, const char* device_name) {
    (void)iot;

    if (!public_key || !device_name) {
        return -1;
    }
    _iot = iot;

    platform_get_property(PROPERTY_DEVICEID, _elevator_id, sizeof(_elevator_id));

    HR_LOGD("%s(%d): elevator id:%s\n", __FUNCTION__, __LINE__, _elevator_id);

    _properties_tbl[PROPERTY_ELEVATOR_ID].dirty = 1;

    // should load from config
    sconf_load_with_proto(ELEVATORD_CONFIG_PATH, &report_switch_conf, 1);
    _properties_tbl[PROPERTY_REPORT_SWITCH].value.val_int64 = report_switch_conf.value.int64;
    // always report when startup
    _properties_tbl[PROPERTY_REPORT_SWITCH].dirty = 1;

    for (size_t i = 0; i < ARRAY_SIZE(_iot_property_topics); i++) {
        struct uviot_topic* t = &_iot_property_topics[i];
        snprintf(t->topic, sizeof(t->topic), "/sys/%s/%s/thing/%s", public_key, device_name, t->name);
        uviot_topic_register(iot, t);
    }

    motion_register_observer(&_iot_property_observer);

    return 0;
}

static void _iot_motion_observer_on_sensor_calibration(struct motion_sensor_calibration_event* data) {
    if (!data)
        return;
    HR_LOGD("%s(%d): sensor:%d, is calibration:%d\n", __FUNCTION__, __LINE__, data->type, data->state);

    if (data->type == SENSOR_ACCELEROMETER) {
        // bias_accel_x
        // bias_accel_y
        // bias_accel_z
        // pitch
        // roll

        _properties_tbl[PROPERTY_IMU_CALIBRATION].value.val_int64 = data->state;
        _properties_tbl[PROPERTY_IMU_CALIBRATION].dirty = 1;

        _properties_tbl[PROPERTY_BIAS_ACCEL_X].value.val_double = data->value[0];
        _properties_tbl[PROPERTY_BIAS_ACCEL_X].dirty = 1;

        _properties_tbl[PROPERTY_BIAS_ACCEL_Y].value.val_double = data->value[1];
        _properties_tbl[PROPERTY_BIAS_ACCEL_Y].dirty = 1;

        _properties_tbl[PROPERTY_BIAS_ACCEL_Z].value.val_double = data->value[2];
        _properties_tbl[PROPERTY_BIAS_ACCEL_Z].dirty = 1;

        _properties_tbl[PROPERTY_BIAS_PITCH].value.val_double = data->value[3];
        _properties_tbl[PROPERTY_BIAS_PITCH].dirty = 1;

        _properties_tbl[PROPERTY_BIAS_ROLL].value.val_double = data->value[4];
        _properties_tbl[PROPERTY_BIAS_ROLL].dirty = 1;
        schedule_report();
    }
}
static void _observer_on_status(struct motion_status* st) {
    if (!st) {
        return;
    }

    // do not allow report property
    if (report_switch_conf.value.int64 == 0) {
        return;
    }

    _realtime_report_times++;
    if (_realtime_report_times % _realtime_report_fac != 0) {
        return;
    }

    _properties_tbl[PROPERTY_FLOOR].value.val_int64 = st->floor;
    _properties_tbl[PROPERTY_FLOOR].dirty = 1;
    _properties_tbl[PROPERTY_HEIGHT].value.val_double = round(st->height * 100) / 100;
    _properties_tbl[PROPERTY_HEIGHT].dirty = 1;
    _properties_tbl[PROPERTY_PRESSURE].value.val_double = round(st->pressure * 10) / 10;
    _properties_tbl[PROPERTY_PRESSURE].dirty = 1;
    _properties_tbl[PROPERTY_TEMPERATURE].value.val_double = round(st->temperature * 10) / 10;
    _properties_tbl[PROPERTY_TEMPERATURE].dirty = 1;

    schedule_report();
}

static void _observer_on_event(struct motion_event* data) {
    if (!data)
        return;

    // do not allow report property
    if (report_switch_conf.value.int64 == 0) {
        return;
    }

    if (data->state != STOPPED && data->state != ACCELERATING)
        return;

    _properties_tbl[PROPERTY_FLOOR].value.val_int64 = data->floor;
    _properties_tbl[PROPERTY_FLOOR].dirty = 1;
    _properties_tbl[PROPERTY_HEIGHT].value.val_double = round(data->height * 100) / 100;
    _properties_tbl[PROPERTY_HEIGHT].dirty = 1;
    _properties_tbl[PROPERTY_PRESSURE].value.val_double = round(data->pressure * 100) / 100;
    _properties_tbl[PROPERTY_PRESSURE].dirty = 1;
    _properties_tbl[PROPERTY_TEMPERATURE].value.val_double = round(data->temperature * 100) / 100;
    _properties_tbl[PROPERTY_TEMPERATURE].dirty = 1;

    schedule_report();
}

// can be called multi times, uv_async will run only once
static void schedule_report(void) {
    uviot_publish_async(_iot, &_iot_property_topics[PROPERTY_TOPIC_POST]);
}

void report_floor_model_property() {
    _properties_tbl[PROPERTY_FLOOR_MODEL].dirty = 1;
    uviot_publish_async(_iot, &_iot_property_topics[PROPERTY_TOPIC_POST]);
}


void report_hqliftd_config_property() {
    _properties_tbl[PROPERTY_HQLIFTD_CONFIG].dirty = 1;
    uviot_publish_async(_iot, &_iot_property_topics[PROPERTY_TOPIC_POST]);
}
