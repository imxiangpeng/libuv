
// mxp, 20250710, implement elevator related iot service

#include "service.h"

#include <cjson/cJSON.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "elevator.h"
#include "hr_log.h"
#include "property.h"
#include "system.h"
#include "topic_property.h"

static int _Reboot(cJSON* params) {
    (void)params;
    // system("sync;reboot");
    system_service_reboot();
    return 0;
}
// {"BaseFloor":1,"FloorsBelow":2,"FloorsAbove":22}
static int _StartAutoFloorCalibration(cJSON* params) {
    int floor_base = 1;
    int floors_below_base = 0;
    int floors_above_base = 1;
    double val = 0;
    if (!params) {
        return -1;
    }

    val = cJSON_GetNumberValue(cJSON_GetObjectItem(params, "BaseFloor"));
    if (isnan(val)) {
        return -1;
    }
    floor_base = (int)val;

    val = cJSON_GetNumberValue(cJSON_GetObjectItem(params, "FloorsBelow"));
    if (isnan(val)) {
        return -1;
    }
    floors_below_base = (int)val;

    val = cJSON_GetNumberValue(cJSON_GetObjectItem(params, "FloorsAbove"));
    if (isnan(val)) {
        return -1;
    }
    floors_above_base = (int)val;

    HR_LOGD("%s(%d): enter calibration: base: %d below: %d above: %d\n", __FUNCTION__, __LINE__, floor_base, floors_below_base, floors_above_base);

    // also  notify floor model
    elevator_floor_enter_calibration(floor_base, floors_below_base, floors_above_base);

    return 0;
}
static int _CalibrateAtFloorManually(cJSON* params) {
    double val = 0;
    val = cJSON_GetNumberValue(cJSON_GetObjectItem(params, "Floor"));
    if (isnan(val)) {
        return -1;
    }

    elevator_floor_calibrate_at_floor((int)val);

    return 0;
}
static int _CalibrateAtHeightManually(cJSON* params) {
    double val = 0;
    val = cJSON_GetNumberValue(cJSON_GetObjectItem(params, "Height"));
    if (isnan(val)) {
        return -1;
    }

    elevator_floor_calibrate_at_height(val);
    return 0;
}

static int _GetHQLiftdConfig(cJSON* params) {
    (void)params;

    properties_tbl[PROPERTY_HQLIFTD_CONFIG].dirty = 1;
    topic_property_report();
    return 0;
}

static int _GetFloorModelData(cJSON* params) {
    (void)params;
    properties_tbl[PROPERTY_FLOOR_MODEL].dirty = 1;
    topic_property_report();
    return 0;
}

static int _SetFloorModelData(cJSON* params) {
    (void)params;
    const char* str = NULL;
    if (!params) {
        return -1;
    }

    str = cJSON_GetStringValue(cJSON_GetObjectItem(params, "data"));

    if (!str) {
        return -1;
    }

    elevator_floor_update_floor_model_data(str);
    return 0;
}

static int _StartSSHTunnel(cJSON* params) {
    (void)params;
    system_service_start_ssh_tunnel();
    return 0;
}

static int _StopSSHTunnel(cJSON* params) {
    (void)params;
    system_service_stop_ssh_tunnel();
    return 0;
}

struct svc_action svc_action_tbl[] = {
    {"StartAutoFloorCalibration", _StartAutoFloorCalibration},
    {"CalibrateAtFloorManually", _CalibrateAtFloorManually},
    {"CalibrateAtHeightManually", _CalibrateAtHeightManually},
    {"GetHQLiftdConfig", _GetHQLiftdConfig},
    {"GetFloorModelData", _GetFloorModelData},
    {"SetFloorModelData", _SetFloorModelData},
    // {"StartSSHTunnel", _StartSSHTunnel},
    // {"StopSSHTunnel", _StopSSHTunnel},
    {"Reboot", _Reboot},
    // {"DoCommand", _DoCommand},
    {NULL, NULL},  // keep it
};

size_t svc_action_tbl_size = sizeof(svc_action_tbl) / sizeof(svc_action_tbl[0]);
