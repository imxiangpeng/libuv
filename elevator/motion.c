#include <errno.h>
#include <float.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "accelerometer_stream.h"
#include "barometer_stream.h"
#include "floor.h"
#include "hr_log.h"
#include "motion.h"
#include "moving_window.h"
#include "sensor.h"
#include "time_utils.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
// please define it when release version
// #define AUTO_FIXED_HEIGHT_WHEN_STOPPING 1

// 海平面标准气压 (Pa)
#define P0 101325.0

// 温度递减率 (K/m)
#define L 0.0065

static const double PRESSURE_L = 0.0065;
static const double PRESSURE_R = 8.31432;
static const double PRESSURE_M = 0.0289644;

static const double G0 = 9.81;

static struct motion_observer* _motion_observers[10] = {0};

static int ACCELEROMETER_SAMPLE_RATE_HZ = 100;
static int BAROMETER_SAMPLE_RATE_HZ = 10;

// 经过测试 3/1/0.5 秒都与加速度以及实际测量值有较大偏差
// 但是这三这个中感觉 1 秒效果比 3/0.5 两个的效果好
static double BAROMETER_WINDOW_DELAY_SECONDS = 1;

// 低于该速度的时候不更新状态，保持原有状态
static double VELOCITY_ZUPT_THRESHOLD = 0.1;

static const double BAROMETER_DISCONTINUOUS_THRESHOLD = 50.0;

static pthread_t _accel_tid = 0;
static pthread_t _barometer_tid = 0;

#define MAX_LINE_LENGTH 1000

#define DUMP_DATA_TO_FILE 1
// simulate using local csv files
#if DUMP_DATA_TO_FILE
static FILE* _dump_fp = NULL;
#endif

#define ACCEL_JITTER_STD_THRESHOLD 0.03

static double barometer_distance = 0;
static double barometer_begin = 0;
static double barometer_end = 0;
static double barometer_pressure = 0;

static double barometer_height_discontinuous = 0;

struct accelerometer_stream {
    struct motion_stream* stream;
    double height;    // --> physical height
    double distance;  // current running distance, maybe reset to zero when running finished
    double velocity;  // velocity, +-

    int is_calibration;

    enum motion_state state;
} _accelerometer_motion;

struct barometer_stream {
    struct motion_stream* stream;
    double height;
    double distance;
    double velocity;

    int is_calibration;

    struct moving_window* mw;
    int64_t delay_stop_ts_ns;
} _barometer_motion;

static int notify_observer(enum motion_observer_action action, void* data);

static const char* motion_state_str(enum motion_state state) {
    switch (state) {
        case STOPPED:
            return "stopped";
        case ACCELERATING:
            return "accelerating";
        case DECELERATING:
            return "decelerating";
        case CONSTANTING:
            return "constanting";
    }
    return "";
}

static double calculate_height_difference(double p0, double p1, double temperature) {
    static double fac = PRESSURE_L * PRESSURE_R / PRESSURE_M / G0;
    return ((temperature + 273.15) / L) * (1 - pow(p1 / p0, fac /*0.190284*/));
}

static void* _accelerometer_thread_routin(void* args) {
    (void)args;
    char buf[MAX_LINE_LENGTH] = {0};
    int64_t delta_time_ns = seconds_to_nanoseconds(1) / ACCELEROMETER_SAMPLE_RATE_HZ;

    int floor_num = 0;
    char floor_label[64] = {0};

#if DUMP_DATA_TO_FILE
    if (_dump_fp) {
        snprintf(buf, sizeof(buf), "now,accel,velocity,distance,height,pressure,pressure_height\n");
        fwrite(buf, 1, strlen(buf), _dump_fp);
    }
#endif

    _accelerometer_motion.is_calibration = -1;
    struct motion_stream* input = _accelerometer_motion.stream;
    double result[8] = {0};
    for (;;) {
        struct timespec spec;
        int64_t now = get_monotonic_nanoseconds();

        int ret = input->read(input, (void*)result, ARRAY_SIZE(result));
        if (ret != 0) {
            // error or calibration not complete
            if (ret == -2 || 1 != input->calibration_completed(input)) {
                HR_LOGD("accelerometer is under calibration\n");
                if (_accelerometer_motion.is_calibration != 1) {
                    _accelerometer_motion.is_calibration = 1;

                    struct motion_sensor_calibration_event ev;

                    memset((void*)&ev, 0, sizeof(ev));
                    ev.type = SENSOR_ACCELEROMETER;
                    ev.is_calibration = 1;
                    notify_observer(MOTION_OBSERVER_ACTION_ON_SENSOR_CALIBRATION, &ev);
                }
            }
            goto next_iteration;
        }

        // read success it mean calibration is finished
        if (_accelerometer_motion.is_calibration != 0) {
            struct motion_sensor_calibration_event ev;

            _accelerometer_motion.is_calibration = 0;

            memset((void*)&ev, 0, sizeof(ev));
            ev.type = SENSOR_ACCELEROMETER;
            ev.is_calibration = 0;
            ev.value[0] = result[4];  // id 4 --> local G
            notify_observer(MOTION_OBSERVER_ACTION_ON_SENSOR_CALIBRATION, &ev);
        }

        double accel = fabs(result[0]);
        double velocity = round(result[1] * 100) / 100;
        double distance = round(result[2] * 1000) / 1000;

        enum motion_state new_state = _accelerometer_motion.state;

        if (velocity == 0) {
            new_state = STOPPED;
        } else if (fabs(velocity) < VELOCITY_ZUPT_THRESHOLD) {
            // keep old state until velocity change significantly
            // new_state = STOPPED;
        } else if (fabs(velocity) > fabs(_accelerometer_motion.velocity)) {
            new_state = ACCELERATING;
        } else if (fabs(velocity) < fabs(_accelerometer_motion.velocity)) {
            new_state = DECELERATING;
        } else {
            new_state = CONSTANTING;
        }

        // use high precision value, not round!
        _accelerometer_motion.velocity = result[1];  // velocity;
        _accelerometer_motion.distance = result[2];  // distance;

        if (new_state != _accelerometer_motion.state) {
            HR_LOGD("motion state: %d -> %d %s ==> %s\n", _accelerometer_motion.state, new_state, motion_state_str(_accelerometer_motion.state), motion_state_str(new_state));
            if (_accelerometer_motion.state == STOPPED) {
                HR_LOGD("mxp starting-------------------from:%d -> %s----->\n", floor_num, floor_label);

                struct motion_event ev = {
                    .state = new_state,
                    .direction = _accelerometer_motion.velocity > 0 ? DIRECTION_UP : DIRECTION_DOWN,
                    .distance = distance,
                };

                notify_observer(MOTION_OBSERVER_ACTION_ON_EVENT, &ev);
            }
            if (new_state == STOPPED) {
                HR_LOGD("stopping------------------------>\n");
                // 推测当前楼层，然后更正高度信息
                // 重置运动模型下次运行数据
                // if (_accelerometer_motion.velocity != 0) {
                input->reset(input);
                //}
                // real height = height + distance
                _accelerometer_motion.height += _accelerometer_motion.distance;
                _accelerometer_motion.distance = 0;
                HR_LOGD("mxp stopping-----------:%f------------->\n", _accelerometer_motion.height);
#if AUTO_FIXED_HEIGHT_WHEN_STOPPING
                if (0 == floor_predict(_accelerometer_motion.height, &floor_num, (char*)&floor_label, sizeof(floor_label))) {
                    HR_LOGD("update height accroding stopping floor relative height\n");
                    HR_LOGD("stopping:-------------------at:%d -> %s----->\n", floor_num, floor_label);
                    double height = _accelerometer_motion.height;
                    if (0 == floor_relative_height(floor_num, &height)) {
                        HR_LOGD("update height accroding stopping floor relative height: %d: %f -> %f\n", floor_num, _accelerometer_motion.height, height);
                        _accelerometer_motion.height = hbarometer_distanceeight;
                    }
                }
#endif
                struct motion_event ev = {
                    .state = new_state,
                    .direction = DIRECTION_NONE,
                    .distance = distance,
                };

                notify_observer(MOTION_OBSERVER_ACTION_ON_EVENT, &ev);
            }
            _accelerometer_motion.state = new_state;
        }

        floor_predict(_accelerometer_motion.height + _accelerometer_motion.distance, &floor_num, (char*)&floor_label, sizeof(floor_label));
        HR_LOGD("accel:%f, velocity:%f, distance:%f, height:%f\n",
                accel, velocity, distance, _accelerometer_motion.height + _accelerometer_motion.distance);

#if DUMP_DATA_TO_FILE
        if (_dump_fp) {
            snprintf(buf, sizeof(buf), "%lf,%f,%f,%f,%f,%f,%f\n", (double)now / 1000000000.0, accel, velocity, distance, _accelerometer_motion.height + _accelerometer_motion.distance, barometer_pressure, barometer_distance);
            fwrite(buf, 1, strlen(buf), _dump_fp);
        }
#endif

        struct motion_status stat = {
            .accel = accel,
            .velocity = velocity,
            .distance = distance,
            .height = _accelerometer_motion.height + _accelerometer_motion.distance,
            .floor = atoi(floor_label),
            .running = (new_state != STOPPED),
        };

        notify_observer(MOTION_OBSERVER_ACTION_ON_STATUS, &stat);

    next_iteration:
        // HR_LOGD("now:%ld, a:%f, stddev:%f, mean:%f\n", now, data.accel_z, stddev, w->mean);
        spec.tv_sec = (now + delta_time_ns) / 1000000000;
        spec.tv_nsec = (now + delta_time_ns) % 1000000000;
        int err;
        do {
            err = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &spec, NULL);
        } while (err < 0 && errno == EINTR);
    }

#if DUMP_DATA_TO_FILE
    if (_dump_fp) {
        fclose(_dump_fp);
        _dump_fp = NULL;
    }
#endif
    HR_LOGD("finished ...\n");
    return NULL;
}

static void* _barometer_thread_routin(void* args) {
    (void)args;
    int64_t delta_time_ns = seconds_to_nanoseconds(1) / BAROMETER_SAMPLE_RATE_HZ;

    struct motion_stream* input = _barometer_motion.stream;
    double result[2] = {0};  // {pressure, temp}

    enum motion_state prev_state = STOPPED;

    if (!_barometer_motion.mw) {
        HR_LOGE("error: can not init moving avg window\n");
        return NULL;
    }

    while (1) {
        struct timespec spec;
        int64_t now = get_monotonic_nanoseconds();

        int ret = input->read(input, (void*)result, ARRAY_SIZE(result));
        if (ret != 0) {
            // error or calibration not complete
            if (ret == -2) {
                HR_LOGD("barometer is under calibration\n");
            }
            goto next_iteration;
        }

        double pressure = result[0];
        double temp = result[1];

        moving_window_update(_barometer_motion.mw, pressure);

        if (barometer_pressure == 0) {
            barometer_pressure = pressure;
        }

        // detect jump
        if (fabs(pressure - barometer_pressure) > BAROMETER_DISCONTINUOUS_THRESHOLD) {
            barometer_height_discontinuous += calculate_height_difference(barometer_pressure, pressure, temp);
            HR_LOGD("%s(%d): mxp jump ... now:%ld, pressure:%f, previous:%f, height:%f\n",
                    __FUNCTION__, __LINE__,
                    now, pressure, barometer_pressure, barometer_height_discontinuous);
        }

        barometer_pressure = pressure;

        if (_accelerometer_motion.state != STOPPED && prev_state == STOPPED) {
            prev_state = _accelerometer_motion.state;
            // we should record last 2 second pressure's avg
            HR_LOGD("mxp last mean:%f vs current %f\n", _barometer_motion.mw->mean, pressure);
            barometer_begin = pressure;
            prev_state = _accelerometer_motion.state;
            _barometer_motion.delay_stop_ts_ns = 0;

            // reset discontinuous value before running
            barometer_height_discontinuous = 0;
        }
        if (_accelerometer_motion.state == STOPPED && prev_state != STOPPED) {
            prev_state = STOPPED;

            _barometer_motion.delay_stop_ts_ns = get_monotonic_nanoseconds() + seconds_to_nanoseconds(BAROMETER_WINDOW_DELAY_SECONDS);
            HR_LOGD("delay finished ....... delay_stop_ts_ns:%ld, current pressure:%f\n", _barometer_motion.delay_stop_ts_ns, pressure);
        }

        // should verify state is stopped
        if (_barometer_motion.delay_stop_ts_ns != 0) {
            if (now > _barometer_motion.delay_stop_ts_ns) {
                _barometer_motion.delay_stop_ts_ns = 0;
                barometer_end = pressure;

                barometer_distance += calculate_height_difference(barometer_begin, barometer_end, temp);

                HR_LOGD("%s(%d): mxp finished ... now:%ld, pressure begin:%f, end:%f, height:%f, discontinus:%f\n",
                        __FUNCTION__, __LINE__,
                        now, barometer_begin, barometer_end, barometer_distance,
                        barometer_height_discontinuous);
                barometer_height_discontinuous = 0;
            }
        }

    next_iteration:
        spec.tv_sec = (now + delta_time_ns) / 1000000000;
        spec.tv_nsec = (now + delta_time_ns) % 1000000000;
        int err;
        do {
            err = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &spec, NULL);
        } while (err < 0 && errno == EINTR);
    }
    return NULL;
}
#if DUMP_DATA_TO_FILE
int dump_data_init() {
    char path[256] = "result-";
    char* ptr = path + strlen(path);
    struct tm tm;
    struct timespec ts;

    clock_gettime(CLOCK_REALTIME, &ts);
    (void)localtime_r(&ts.tv_sec, &tm);

    strftime(ptr, sizeof(path) - strlen(path) - 1, "%Y-%m-%d-%H-%M-%S", &tm);
    strcat(path, ".csv");
    HR_LOGD("dump path:%s\n", path);

    FILE* fp = fopen(path, "w+");
    if (!fp) {
        perror("open error:");
        fclose(fp);
        return -1;
    }

    _dump_fp = fp;

    return 0;
}
#endif

int motion_initalize(int argc, char** argv) {
    (void)argc;
    (void)argv;
    int ret = -1;

#if DUMP_DATA_TO_FILE
    dump_data_init();
#endif

    memset((void*)&_accelerometer_motion, 0, sizeof(_accelerometer_motion));

    _accelerometer_motion.stream = accelerometer_stream_init(ACCELEROMETER_SAMPLE_RATE_HZ);
    // simulate data, initialize floor
    // _accelerometer_motion.height = -6.1;

    if (!_accelerometer_motion.stream) {
        HR_LOGE("can not find accelerometer ...\n");
        return -1;
    }

    ret = _accelerometer_motion.stream->open(_accelerometer_motion.stream);
    if (ret != 0) {
        // release
        return -1;
    }

    // which floor are we current stopping at?
    // should update height ?
    // or we should force wait base floor trigger
    _accelerometer_motion.stream->enter_calibration(_accelerometer_motion.stream);

    _barometer_motion.stream = barometer_stream_init(BAROMETER_SAMPLE_RATE_HZ);
    if (!_barometer_motion.stream) {
        HR_LOGE("can not find barometer ...\n");
        return -1;
    }

    ret = _barometer_motion.stream->open(_barometer_motion.stream);
    if (ret != 0) {
        // release
        return -1;
    }

    _barometer_motion.mw = moving_window_init(BAROMETER_SAMPLE_RATE_HZ * BAROMETER_WINDOW_DELAY_SECONDS);
    if (!_barometer_motion.mw) {
        return -1;
    }

    // acceleration_initialize();
    // barometer_initialize();
    return 0;
}

static int motion_acceleration_start(void) {
    int ret = 0;
    pthread_attr_t attr;
    struct sched_param param;
    int thread_policy;
    const int algorithm = SCHED_FIFO;  // SCHED_RR

    if (_accel_tid != 0) {
        return -1;
    }

    pthread_attr_init(&attr);

    // pthread_attr_getschedpolicy(&attr, &thread_policy);
    // pthread_attr_getschedparam(&attr, &param);
    ret = pthread_attr_setschedpolicy(&attr, algorithm);
    if (0 != ret) {
        HR_LOGE("%s(%d): failed to pthread_attr_setschedpolicy\n", __FUNCTION__, __LINE__);
        return -1;
    }

    param.sched_priority = sched_get_priority_max(algorithm);
    ret = pthread_attr_setschedparam(&attr, &param);
    if (ret != 0) {
        perror("pri:");
        HR_LOGD("failed:%d ...\n", ret);
    }

    HR_LOGD("max level:%d\n", param.sched_priority);
    /*ret = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (0 != ret) {
        HR_LOGE("%s(%d): failed to pthread_attr_setdetachstate\n", __FUNCTION__, __LINE__);
        return -1;
    }*/

    ret = pthread_create(&_accel_tid, &attr, _accelerometer_thread_routin, NULL);
    if (0 != ret) {
        HR_LOGE("%s(%d): failed to pthread_create\n", __FUNCTION__, __LINE__);
        return -1;
    }

    ret = pthread_getschedparam(_accel_tid, &thread_policy, &param);
    if (ret != 0) {
        perror("pri:");
        HR_LOGD("failed ...\n");
    }
    HR_LOGD("thread policy is %s, priority is %d\n",
            ((thread_policy == SCHED_FIFO) ? "FIFO" : (thread_policy == SCHED_RR ? "RR" : (thread_policy == SCHED_OTHER ? "OTHER" : "unknown"))), param.sched_priority);

    pthread_attr_destroy(&attr);

    return 0;
}

static int motion_barometer_start(void) {
    int ret = 0;
    pthread_attr_t attr;

    if (_barometer_tid != 0) {
        return -1;
    }

    pthread_attr_init(&attr);

    ret = pthread_create(&_barometer_tid, &attr, _barometer_thread_routin, NULL);
    if (0 != ret) {
        HR_LOGE("%s(%d): failed to pthread_create\n", __FUNCTION__, __LINE__);
        return -1;
    }
    pthread_attr_destroy(&attr);

    return 0;
}

int motion_run(void) {
    motion_barometer_start();
    motion_acceleration_start();
    // wait device still

    //  HR_LOGD("now device is ready ...\n");

    return 0;
}

int motion_deinitalize(void) {
    if (_barometer_tid != 0) {
        pthread_cancel(_barometer_tid);
        pthread_join(_barometer_tid, NULL);
        _barometer_tid = 0;
    }

    if (_accel_tid != 0) {
        pthread_cancel(_accel_tid);
        pthread_join(_accel_tid, NULL);
        _accel_tid = 0;
    }

    _accelerometer_motion.stream->close(_accelerometer_motion.stream);
    accelerometer_stream_deinit(_accelerometer_motion.stream);
    _accelerometer_motion.stream = NULL;
    _barometer_motion.stream->close(_barometer_motion.stream);
    barometer_stream_deinit(_barometer_motion.stream);
    _barometer_motion.stream = NULL;
    if (_barometer_motion.mw) {
        moving_window_release(_barometer_motion.mw);
        _barometer_motion.mw = NULL;
    }

#if DUMP_DATA_TO_FILE
    if (_dump_fp) {
        fclose(_dump_fp);
        _dump_fp = NULL;
    }
#endif
    return 0;
}
int motion_register_observer(struct motion_observer* observer) {
    size_t i = 0;

    for (i = 0; i < ARRAY_SIZE(_motion_observers); i++) {
        struct motion_observer* obs = _motion_observers[i];
        if (!obs) {
            _motion_observers[i] = observer;
            return 0;
        } else {
            if (obs == observer) {
                // already exists!
                return 0;
            }
        }
    }

    return -1;
}

// should use lock?
int motion_unregister_observer(struct motion_observer* observer) {
    size_t i = 0;

    for (i = 0; i < ARRAY_SIZE(_motion_observers); i++) {
        struct motion_observer* obs = _motion_observers[i];
        if (obs == observer) {
            _motion_observers[i] = 0;
            // already exists!
            return 0;
        }
    }

    return -1;
}

int motion_enter_sensor_calibration() {
    // not only support accelerometer
    if (_accelerometer_motion.stream) {
        _accelerometer_motion.stream->enter_calibration(_accelerometer_motion.stream);
    }

    return 0;
}
int motion_calibrate_at_floor(int floor) {
    double height = 0;
    if (floor_relative_height(floor, &height) != 0) {
        HR_LOGD("%s(%d): calibrate at floor %d failed\n", __FUNCTION__, __LINE__, floor);
        return -1;
    }

    _accelerometer_motion.height = height;

    HR_LOGD("%s(%d): calibrate at floor %d -> height: %f success\n", __FUNCTION__, __LINE__, floor, height);
    return 0;
}

int motion_calibrate_at_height(double height) {
    int floor;
    char label[64] = {0};
    _accelerometer_motion.height = height;

    if (0 != floor_predict(height, &floor, label, sizeof(label))) {
        HR_LOGD("%s(%d): calibrate at height %f failed\n", __FUNCTION__, __LINE__, height);
        return -1;
    }
    HR_LOGD("%s(%d): calibrate at height %f -> floor: %d(%s) success\n", __FUNCTION__, __LINE__, height, floor, label);
    return 0;
}
static int notify_observer(enum motion_observer_action action, void* data) {
    size_t i = 0;

    for (i = 0; i < ARRAY_SIZE(_motion_observers); i++) {
        struct motion_observer* obs = _motion_observers[i];
        if (obs) {
            switch (action) {
                case MOTION_OBSERVER_ACTION_ON_STATUS:
                    if (obs->on_status) {
                        obs->on_status((struct motion_status*)data);
                    }
                    break;
                case MOTION_OBSERVER_ACTION_ON_EVENT:
                    if (obs->on_event) {
                        obs->on_event((struct motion_event*)data);
                    }
                    break;
                case MOTION_OBSERVER_ACTION_ON_SENSOR_CALIBRATION:
                    if (obs->on_sensor_calibration) {
                        obs->on_sensor_calibration((struct motion_sensor_calibration_event*)data);
                    }
                    break;
                default:
                    break;
            }
        }
    }
    return 0;
}
