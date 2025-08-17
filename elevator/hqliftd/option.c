// mxp, 20250721, orignize option in one file
#include "option.h"

#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "elevator.h"
#include "hr_log.h"

#define DEFAULT_BROKER_PORT 1883     // 8883 //1883
#define DEFAULT_BROKER_ALIVETIME 60  // 300 //60                       // 60s

// not limit kunren & ebike event
#define LIFTFAULT_REPORT_LIMIT_PER_DAY 3

#define RTMP_URL_PREFIX "rtmp://srs.hqszjs.com:1935/live"

static char DEFAULT_MQ_ID[128] = {0};  // please init id when initialize
static char DEFAULT_LIVE_URL[512] = {0};  // please init id when initialize

static char DEFAULT_MQ_PROTO[] = "mqtt";
static char DEFAULT_BROKER_SERVER[] = "mq.hqszjs.com";
static char DEFAULT_MQ_USERNAME[] = "inspur";
static char DEFAULT_MQ_PASSWORD[] = "inspur88*";

static char DEFAULT_FTP_ADDRESS[] = "ftp://ftp.hqszjs.com:2100";
static char DEFAULT_FTP_USERNAME[] = "inspur";
static char DEFAULT_FTP_PASSWORD[] = "inspur88*";

struct sconf_proto _options[_OPTION_MAX] = {
    // MQTT
    [OPTION_MQ_ID] = {"MQ_ID", PROTO_VALUE_STRING, {.string = DEFAULT_MQ_ID}},
    [OPTION_MQ_PROTO] = {"MQ_PROTO", PROTO_VALUE_STRING, {.string = DEFAULT_MQ_PROTO}},
    [OPTION_MQ_SERVER] = {"MQ_SERVER", PROTO_VALUE_STRING, {.string = DEFAULT_BROKER_SERVER}},
    [OPTION_MQ_PORT] = {"MQ_PORT", PROTO_VALUE_NUMBER, {.number = DEFAULT_BROKER_PORT}},
    [OPTION_MQ_KEEPALIVE] = {"MQ_KEEPALIVQE", PROTO_VALUE_NUMBER, {.number = DEFAULT_BROKER_ALIVETIME}},
    [OPTION_MQ_USERNAME] = {"MQ_USERNAME", PROTO_VALUE_STRING, {.string = DEFAULT_MQ_USERNAME}},
    [OPTION_MQ_PASSWORD] = {"MQ_PASSWORD", PROTO_VALUE_STRING, {.string = DEFAULT_MQ_PASSWORD}},

    // rtmp
    [OPTION_LIVE_URL] = {"LIVE_URL", PROTO_VALUE_STRING, {.string = DEFAULT_LIVE_URL}},

    // FTP
    [OPTION_FTP_ADDRESS] = {"FTP_ADDRESS", PROTO_VALUE_STRING, {.string = DEFAULT_FTP_ADDRESS}},
    [OPTION_FTP_USERNAME] = {"FTP_USERNAME", PROTO_VALUE_STRING, {.string = DEFAULT_FTP_USERNAME}},
    [OPTION_FTP_PASSWORD] = {"FTP_PASSWORD", PROTO_VALUE_STRING, {.string = DEFAULT_FTP_PASSWORD}},

    // LiftState
    [OPTION_REALTIME_REPORT_PERIOD_MS] = {"REALTIME_REPORT_PERIOD_MS", PROTO_VALUE_NUMBER, {.number = 1000}},
    // speed
    [OPTION_SPEED_LIMIT_THRESHOLD] = {"SPEED_LIMIT_THREHOLD", PROTO_VALUE_DECIMAL, {.decimal = 2.8}},  // 2.8m/s

    // LiftRunInfo
    [OPTION_RUNINFO_REPORT_SWITCH] = {"RUNINFO_REPORT_SWITCH", PROTO_VALUE_NUMBER, {.number = 1}},
    // LiftFault
    [OPTION_FAULT_REPORT_SWITCH] = {"LIFTFAULT_REPORT_SWITCH", PROTO_VALUE_NUMBER, {.number = 1}},
    // [OPTION_FAULT_SPEED_LIMIT_THRESHOLD]
    [OPTION_FAULT_VIDEO_UPLOAD_SWITCH] = {"LIFTFAULT_VIDEO_UPLOAD_SWITCH", PROTO_VALUE_NUMBER, {.number = 1}},
    [OPTION_FAULT_REPORT_LIMIT_PER_DAY] = {"LIFTFAULT_REPORT_LIMIT_PER_DAY", PROTO_VALUE_NUMBER, {.number = LIFTFAULT_REPORT_LIMIT_PER_DAY}},  // default 3
};

struct sconf_proto _eguard_options[_OPTION_EGUARD_MAX] = {
    [OPTION_EGUARD_KUNREN_DETECT_ENABLED] = {"EGUARD_KUNREN_DETECT_ENABLED", PROTO_VALUE_NUMBER, {.number = 1}},
    [OPTION_EGUARD_KUNREN_DETECT_TIMEOUT] = {"EGUARD_KUNREN_DETECT_TIMEOUT", PROTO_VALUE_NUMBER, {.number = 90000}},            // 90min
    [OPTION_EGUARD_DOOR_ZONE_STOPPED_THRESHOLD] = {"EGUARD_DOOR_ZONE_STOPPED_THRESHOLD", PROTO_VALUE_DECIMAL, {.decimal = 0}},  // default not enabled
};

static void load_option() {
    // int64_t realtime_period_ms = _options[OPTION_REALTIME_REPORT_PERIOD_MS].value.number;
    // please manually process default string value
    // 要这么复杂吗，还是说每次都是动态申请，然后每次强制释放

    if (_options[OPTION_MQ_PROTO].value.string != DEFAULT_MQ_PROTO) {
        printf("%s(%d): need free\n", __FUNCTION__, __LINE__);
        if (_options[OPTION_MQ_PROTO].value.string) {
            free(_options[OPTION_MQ_PROTO].value.string);
            _options[OPTION_MQ_PROTO].value.string = NULL;
        }
    } else {
        printf("%s(%d): no need free\n", __FUNCTION__, __LINE__);
    }

    if (_options[OPTION_MQ_ID].value.string != DEFAULT_MQ_ID) {
        printf("%s(%d): need free\n", __FUNCTION__, __LINE__);
        if (_options[OPTION_MQ_ID].value.string) {
            free(_options[OPTION_MQ_ID].value.string);
            _options[OPTION_MQ_ID].value.string = NULL;
        }
    } else {
        printf("%s(%d): no need free\n", __FUNCTION__, __LINE__);
    }

    if (_options[OPTION_MQ_SERVER].value.string != DEFAULT_BROKER_SERVER) {
        printf("%s(%d): need free\n", __FUNCTION__, __LINE__);
        if (_options[OPTION_MQ_SERVER].value.string) {
            free(_options[OPTION_MQ_SERVER].value.string);
            _options[OPTION_MQ_SERVER].value.string = NULL;
        }
    } else {
        printf("%s(%d): no need free\n", __FUNCTION__, __LINE__);
    }

    if (_options[OPTION_MQ_USERNAME].value.string != DEFAULT_MQ_USERNAME) {
        printf("%s(%d): need free\n", __FUNCTION__, __LINE__);
        if (_options[OPTION_MQ_USERNAME].value.string) {
            free(_options[OPTION_MQ_USERNAME].value.string);
            _options[OPTION_MQ_USERNAME].value.string = NULL;
        }
    } else {
        printf("%s(%d): no need free\n", __FUNCTION__, __LINE__);
    }

    if (_options[OPTION_MQ_PASSWORD].value.string != DEFAULT_MQ_PASSWORD) {
        printf("%s(%d): need free\n", __FUNCTION__, __LINE__);
        if (_options[OPTION_MQ_PASSWORD].value.string) {
            free(_options[OPTION_MQ_PASSWORD].value.string);
            _options[OPTION_MQ_PASSWORD].value.string = NULL;
        }
    } else {
        printf("%s(%d): no need free\n", __FUNCTION__, __LINE__);
    }

    if (_options[OPTION_LIVE_URL].value.string != DEFAULT_LIVE_URL) {
        printf("%s(%d): need free\n", __FUNCTION__, __LINE__);
        if (_options[OPTION_LIVE_URL].value.string) {
            free(_options[OPTION_LIVE_URL].value.string);
            _options[OPTION_LIVE_URL].value.string = NULL;
        }
    } else {
        printf("%s(%d): no need free\n", __FUNCTION__, __LINE__);
    }

    if (_options[OPTION_FTP_ADDRESS].value.string != DEFAULT_FTP_ADDRESS) {
        printf("%s(%d): need free\n", __FUNCTION__, __LINE__);
        if (_options[OPTION_FTP_ADDRESS].value.string) {
            free(_options[OPTION_FTP_ADDRESS].value.string);
            _options[OPTION_FTP_ADDRESS].value.string = NULL;
        }
    } else {
        printf("%s(%d): no need free\n", __FUNCTION__, __LINE__);
    }

    if (_options[OPTION_FTP_USERNAME].value.string != DEFAULT_FTP_USERNAME) {
        printf("%s(%d): need free\n", __FUNCTION__, __LINE__);
        if (_options[OPTION_FTP_USERNAME].value.string) {
            free(_options[OPTION_FTP_USERNAME].value.string);
            _options[OPTION_FTP_USERNAME].value.string = NULL;
        }
    } else {
        printf("%s(%d): no need free\n", __FUNCTION__, __LINE__);
    }

    if (_options[OPTION_FTP_PASSWORD].value.string != DEFAULT_FTP_PASSWORD) {
        printf("%s(%d): need free\n", __FUNCTION__, __LINE__);
        if (_options[OPTION_FTP_PASSWORD].value.string) {
            free(_options[OPTION_FTP_PASSWORD].value.string);
            _options[OPTION_FTP_PASSWORD].value.string = NULL;
        }
    } else {
        printf("%s(%d): no need free\n", __FUNCTION__, __LINE__);
    }

    sconf_load_with_proto(HQLIFTD_CONFIG_PATH, _options, sizeof(_options) / sizeof(_options[0]));

    // printf("mqtt:%s, %s, %s\n", _options[OPTION_MQ_SERVER].value.string, _options[OPTION_MQ_USERNAME].value.string, _options[OPTION_MQ_PASSWORD].value.string);
    // printf("ftp:%s, %s, %s\n", _options[OPTION_FTP_ADDRESS].value.string, _options[OPTION_FTP_USERNAME].value.string, _options[OPTION_FTP_PASSWORD].value.string);

    for (size_t i = 0; i < sizeof(_options) / sizeof(_options[0]); i++) {
        switch (_options[i].type) {
            case PROTO_VALUE_NUMBER:
                HR_LOGD("_option -> %s:%ld\n", _options[i].name, _options[i].value.number);
                break;
            case PROTO_VALUE_DECIMAL:
                HR_LOGD("_option -> %s:%f\n", _options[i].name, _options[i].value.decimal);
                break;
            case PROTO_VALUE_STRING:
                HR_LOGD("_option -> %s:%s\n", _options[i].name, _options[i].value.string ? _options[i].value.string : "");
                break;
        }
    }
    // realtime report period need restart
    // if (realtime_period_ms != _options[OPTION_REALTIME_REPORT_PERIOD_MS].value.number) {
    //     // we should restart
    //
    //     exit(0);
    // }
}

static void load_eguard_option() {
    sconf_load_with_proto(EGUARD_CONFIG_PATH, _eguard_options, sizeof(_eguard_options) / sizeof(_eguard_options[0]));

    for (size_t i = 0; i < sizeof(_eguard_options) / sizeof(_eguard_options[0]); i++) {
        switch (_eguard_options[i].type) {
            case PROTO_VALUE_NUMBER:
                HR_LOGD("_eguard_options -> %s:%ld\n", _eguard_options[i].name, _eguard_options[i].value.number);
                break;
            case PROTO_VALUE_DECIMAL:
                HR_LOGD("_eguard_options -> %s:%f\n", _eguard_options[i].name, _eguard_options[i].value.decimal);
                break;
            case PROTO_VALUE_STRING:
                HR_LOGD("_eguard_options -> %s:%s\n", _eguard_options[i].name, _eguard_options[i].value.string ? _eguard_options[i].value.string : "");
                break;
        }
    }

    if (_eguard_options[OPTION_EGUARD_KUNREN_DETECT_TIMEOUT].value.number < 30000) {
        _eguard_options[OPTION_EGUARD_KUNREN_DETECT_TIMEOUT].value.number = 30000;
    }

    if (_eguard_options[OPTION_EGUARD_DOOR_ZONE_STOPPED_THRESHOLD].value.decimal < 0) {
        _eguard_options[OPTION_EGUARD_DOOR_ZONE_STOPPED_THRESHOLD].value.decimal = 0;
    }
}
static void on_option_changed(const char* path, void* priv) {
    (void)priv;
    if (!path) return;

    if (0 == strcmp(HQLIFTD_CONFIG_PATH, path)) {
        load_option();
    } else if (0 == strcmp(EGUARD_CONFIG_PATH, path)) {
        load_eguard_option();
    }
}

int option_init(void) {
    const char* serialno = elevator_serialno();  // support called without init
    // no need verfiy null, because serialno should point to inner static buffer
    if (!serialno || 0 == strlen(serialno)) {
        HR_LOGE("serial no should not be empty\n");
        assert(0);
        return -1;
    }

    strncpy(DEFAULT_MQ_ID, serialno, sizeof(DEFAULT_MQ_ID));
    snprintf(DEFAULT_LIVE_URL, sizeof(DEFAULT_LIVE_URL), RTMP_URL_PREFIX "/%s", serialno);

    load_option();

    // verify config has been initialized
    if (_options[OPTION_MQ_ID].value.string == DEFAULT_MQ_ID /*&&
        _options[OPTION_MQ_SERVER].value.string == DEFAULT_BROKER_SERVER*/
    ) {
        HR_LOGD("hqliftd has too many default parameters, we should write them\n");
        sconf_save_with_proto(HQLIFTD_CONFIG_PATH, _options, sizeof(_options) / sizeof(_options[0]));
    }

    load_eguard_option();

    sconf_register_observer(EGUARD_CONFIG_PATH, on_option_changed, NULL);
    // sconf_register_observer(HQLIFTD_CONFIG_PATH, on_option_changed, NULL);

    return 0;
}
