#ifndef SERVICE_H
#define SERVICE_H
#include <cjson/cJSON.h>

#if 0
enum {
    CALIBRATION_TOPIC_AUTO_FLOOR_CALIBRATION_EVENT = 0,
    CALIBRATION_TOPIC_START_AUTO_FLOOR_CALIBRATION,
    CALIBRATION_TOPIC_CALIBRATE_AT_FLOOR_MANUALLY,
    CALIBRATION_TOPIC_CALIBRATE_AT_HEIGHT_MANUALLY,
    GET_HQLIFTD_CONFIG,
    // GET_HQLIFTD_CONFIG_REPLY,
    GET_FLOOR_MODEL,
    SET_FLOOR_MODEL,
    START_SSH_TUNNEL,
    STOP_SSH_TUNNEL,
    REBOOT,
    DO_COMMAND,
    _SERVICE_TOPIC_MAX
};
#endif
struct svc_action {
    const char* name;
    int (*method)(cJSON* param);
};

extern struct svc_action svc_action_tbl[];
extern size_t svc_action_tbl_size;
#endif