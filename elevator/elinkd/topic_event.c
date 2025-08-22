
// mxp, 20250711, implement elevator AutoFloorCalibrationEvent event

#include <cjson/cJSON.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <libubox/list.h>

#include "elevator.h"
#include "topic_event.h"
#include "iot.h"

#include "hr_log.h"
#include "topic.h"
static pthread_mutex_t _queue_mutex;

static LIST_HEAD(_auto_floor_calibration_message_queue);

static int _on_auto_floor_calibration_event_publish(void** payload, int* len);

struct topic auto_floor_calibration_event_post = {
    .name = "event/AutoFloorCalibrationEvent/post",
    .topic = {0},
    .period = 0,
    .qos = 2,
    .type = TOPIC_TYPE_PUBLISH,
    .callback.on_publish = _on_auto_floor_calibration_event_publish,
};

struct floor_calibration_event* floor_calibration_event_alloc() {
    struct floor_calibration_event* e = (struct floor_calibration_event*)calloc(1, sizeof(struct floor_calibration_event));
    if (!e) {
        return NULL;
    }

    INIT_LIST_HEAD(&e->entry);

    return e;
}

static void calibration_event_free(struct floor_calibration_event* e) {
    if (!e) {
        return;
    }

    pthread_mutex_lock(&_queue_mutex);
    list_del(&e->entry);
    pthread_mutex_unlock(&_queue_mutex);

    INIT_LIST_HEAD(&e->entry);

    free(e);
}

int send_floor_calibration_event(struct floor_calibration_event* m) {
    pthread_mutex_lock(&_queue_mutex);
    list_add_tail(&m->entry, &_auto_floor_calibration_message_queue);
    pthread_mutex_unlock(&_queue_mutex);

    return iot_topic_publish_async(&auto_floor_calibration_event_post);
}

static int _on_auto_floor_calibration_event_publish(void** payload, int* len) {
    char tmp[256] = {0};
    struct floor_calibration_event* e = NULL;
    cJSON *root = NULL, *param = NULL;

    // not new message, no need publish
    if (list_empty(&_auto_floor_calibration_message_queue)) {
        return -1;
    }

    list_for_each_entry(e, &_auto_floor_calibration_message_queue, entry) {
        HR_LOGE("%s(%d): mxp id:%d, floor:%d, label:%s, height:%f, pressure:%f\n", __FUNCTION__, __LINE__, e->id, e->floor, e->label, e->height, e->pressure);
    }

    e = list_first_entry(&_auto_floor_calibration_message_queue, struct floor_calibration_event, entry);

    root = cJSON_CreateObject();
    if (!root) {
        calibration_event_free(e);
        return -1;
    }

    snprintf(tmp, sizeof(tmp), "%d", topic_generate_mid());
    cJSON_AddStringToObject(root, "id", tmp);
    cJSON_AddStringToObject(root, "version", "1.0.0");

    param = cJSON_AddObjectToObject(root, "params");
    cJSON_AddNumberToObject(param, "Id", e->id);
    cJSON_AddNumberToObject(param, "Floor", e->floor);
    cJSON_AddStringToObject(param, "Label", e->label);
    cJSON_AddNumberToObject(param, "Height", e->height);
    cJSON_AddNumberToObject(param, "Pressure", e->pressure);

    // free the message
    calibration_event_free(e);

    *payload = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!*payload)
        return -1;

    *len = strlen(*payload);
    HR_LOGD("publish: %s\n", (char*)*payload);
#if 1
    if (!list_empty(&_auto_floor_calibration_message_queue)) {
        // when queue is not empty, we should trigger again
        // because uv_async merges multiple requests and triggers the callback only once
        return iot_topic_publish_async(&auto_floor_calibration_event_post);
    }
#endif
    return 0;
}

int topic_event_init(const char* public_key, const char* device_name) {
    snprintf(auto_floor_calibration_event_post.topic, sizeof(auto_floor_calibration_event_post.topic), "/sys/%s/%s/thing/%s", public_key, device_name, auto_floor_calibration_event_post.name);
    iot_topic_register(&auto_floor_calibration_event_post);
    return 0;
}