
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "file_util.h"
#include "iot_topic.h"
#include "cjson/cJSON.h"
#include "hr_log.h"

#include "motion.h"
#include "sensor.h"

#include "uviot.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

enum {
    PROPERTY_TOPIC_POST = 0,
    PROPERTY_TOPIC_SET,
    _PROPERTY_TOPIC_MAX,
};

static struct uviot* _iot = NULL;

static const char* _elevator_id_pending = NULL;

static int _property_imu_calibration = 0;
static int _property_imu_calibration_reported = 0;
static double _property_G = 9.81;
static double _property_G_reported = 9.81;

static int _realtime_report_times = 0;
static const int _realtime_report_fac = 100;  // 10 * sampling_rate = 100 * 1/100 = 1s

static double _status_pressure = 0;
static double _status_pressure_reported = 0;
static double _status_temperature = 20.3;
static double _status_temperature_reported = 20.3;
static int _status_floor = 0;
static int _status_floor_reported = 0;
// static int _status_door = 0;
static double _status_speed = 0;
static double _status_speed_reported = 0;
static double _status_height = 0;
static double _status_height_reported = 0;
// static int _status_direction = 0;

static int _property_floor_model_pending = 0;

static void _iot_motion_observer_on_sensor_calibration(struct motion_sensor_calibration_event* data);
static void _observer_on_status(struct motion_status* st);

static struct motion_observer _iot_property_observer = {
    .on_status = _observer_on_status,
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
    if (_status_pressure_reported != _status_pressure) {
        cJSON_AddNumberToObject(param, "pressure", _status_pressure);
        _status_pressure_reported = _status_pressure;
    }
    if (_status_temperature_reported != _status_temperature) {
        cJSON_AddNumberToObject(param, "temperature", _status_temperature);
        _status_temperature_reported = _status_temperature;
    }
    if (_property_imu_calibration_reported != _property_imu_calibration) {
        cJSON_AddNumberToObject(param, "imu_calibration", _property_imu_calibration);
        _property_imu_calibration_reported = _property_imu_calibration;
    }
    if (_property_G_reported != _property_G) {
        cJSON_AddNumberToObject(param, "G", _property_G);
        _property_G_reported = _property_G;
    }
    if (_status_speed_reported != _status_speed) {
        cJSON_AddNumberToObject(param, "speed", _status_speed);
        _status_speed_reported = _status_speed;
    }

    if (_status_height_reported != _status_height) {
        cJSON_AddNumberToObject(param, "height", _status_height);
        _status_height_reported = _status_height;
    }

    if (_status_floor_reported != _status_floor) {
        cJSON_AddNumberToObject(param, "floor", _status_floor);
        _status_floor_reported = _status_floor;
    }

    if (_elevator_id_pending) {
        cJSON_AddStringToObject(param, "elevator_id", _elevator_id_pending);
        _elevator_id_pending = NULL;
    }

    if (_property_floor_model_pending) {
        char* data = NULL;
        _property_floor_model_pending = 0;
        futil_read("./floor_model_generated.json", &data);

        cJSON_AddStringToObject(param, "floor_model", data);
        HR_LOGD("property report floor model:%s\n", data);
        free(data);
    }

    *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!*payload)
        return -1;

    *len = strlen(*payload);
    HR_LOGD("publish: %s\n", *payload);
#if 0
    *payload = strdup("{\"name\":\"xiaohua\"}");
    *len = strlen(*payload);
#endif
    return 0;
}

static int _on_reply_message(void* payload, int len) {
    printf("reply message %d -> %s\n", len, (char*)payload);
    return 0;
}

static int _on_property_set_message(void* payload, int len) {
    printf("set message %d -> %s\n", len, (char*)payload);
    char* method = NULL;
    double val = 0;
    const char* elevator_id = NULL;
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
        HR_LOGD("%s(%d): enter calibration val: %d\n", __FUNCTION__, __LINE__, val);
        if (val == 1) {
            motion_enter_sensor_calibration();
        }
    }

    elevator_id = cJSON_GetStringValue(cJSON_GetObjectItem(params, "elevator_id"));
    if (elevator_id) {
        // burn elevator id
    }
    cJSON_Delete(root);

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
    }};

int iot_topic_property_init(struct uviot* iot, const char* public_key, const char* device_name) {
    (void)iot;
    if (!public_key || !device_name) {
        return -1;
    }
    _iot = iot;
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
    HR_LOGD("%s(%d): sensor:%d, is calibration:%d\n", __FUNCTION__, __LINE__, data->type, data->is_calibrating);

    if (data->type == SENSOR_ACCELEROMETER) {
        _property_imu_calibration = data->is_calibrating;
        _property_G = round(data->value[0] * 10000) / 10000;
        HR_LOGD("%s(%d): sensor:%d, is calibration:%d, G:%f vs %f\n", __FUNCTION__, __LINE__, data->type, data->is_calibrating, _property_G, data->value[0]);
        uviot_publish_async(_iot, &_iot_property_topics[PROPERTY_TOPIC_POST]);
    }
}

static void _observer_on_status(struct motion_status* st) {
    int need_publish = 0;

    if (!st)
        return;
    // HR_LOGD("speed : %f\n", _speed_realtime);

    HR_LOGD("_report times:%d\n", _realtime_report_times);
    if (_realtime_report_times % _realtime_report_fac == 0) {
        _realtime_report_times = 0;

        if (_status_speed != fabs(st->velocity)) {
            _status_speed = round(fabs(st->velocity) * 1000) / 1000;
            need_publish |= 1;
        }
        if (_status_height != st->height) {
            _status_height = round(st->height * 100) / 100;
        }
        if (_status_floor != st->floor) {
            need_publish |= 1;
            _status_floor = st->floor;
        }
        if (_status_pressure != st->pressure) {
            _status_pressure = round(st->pressure);
            need_publish |= 1;
        }
    }

    _realtime_report_times++;

    if (0 != need_publish) {
        uviot_publish_async(_iot, &_iot_property_topics[PROPERTY_TOPIC_POST]);
    }
}

void report_floor_model_property() {
    _property_floor_model_pending = 1;
    uviot_publish_async(_iot, &_iot_property_topics[PROPERTY_TOPIC_POST]);
}