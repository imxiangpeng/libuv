#include <errno.h>
#include <float.h>
#include <math.h>
// #include <ncurses.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "accelerometer_motion.h"
#include "barometer_motion.h"
#include "core.h"
#include "floor.h"
#include "hr_log.h"
#include "sensors/sensor.h"
#include "time_utils.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
// #define AUTO_FIXED_HEIGHT_WHEN_STOPPING 1

// 海平面标准气压 (Pa)
#define P0 101325.0

// 温度递减率 (K/m)
#define L 0.0065

static const double PRESSURE_L = 0.0065;
static const double PRESSURE_R = 8.31432;
static const double PRESSURE_M = 0.0289644;

static const double G0 = 9.81;

static struct core_observer* _observers[10] = {0};
static int ACCELEROMETER_SAMPLE_RATE_HZ = 100;
static int BAROMETER_SAMPLE_RATE_HZ = 20;

// 经过测试 3/1/0.5 秒都与加速度以及实际测量值有较大偏差
// 但是这三这个中感觉 1 秒效果比 3/0.5 两个的效果好
static double BAROMETER_WINDOW_DELAY_SECONDS = 1;

static double MOVEMENT_THRESHOLD = 0.1f;
static double VELOCITY_ZUPT_THRESHOLD = 0.1f;

static pthread_t _accel_tid = 0;
static pthread_t _barometer_tid = 0;

static double _G = 9.823;

#define MAX_LINE_LENGTH 1000

#define DUMP_DATA_TO_FILE 1
// simulate using local csv files
#if DUMP_DATA_TO_FILE
static FILE* _dump_fp = NULL;
#endif

// -1: not enough data, fill again
// 0: not stable
// 1: stable
enum {
    JITTER_UNKNOWN = 0,
    JITTER_STABLE,
    JITTER_UNSTABLE
};

typedef enum {
    STATE_STILL,
    STATE_MOVING_UP,
    STATE_MOVING_DOWN,

} motion_state_e;

typedef enum {
    ELEVATOR_UNKNOWN = 0,
    ELEVATOR_STOPPED,
    ELEVATOR_STARTING,
    ELEVATOR_CONSTANT,
    ELEVATOR_SLOWING
} ElevatorState;

static ElevatorState _elevator_state = ELEVATOR_STOPPED;

#define ACCEL_JITTER_STD_THRESHOLD 0.03

struct moving_window {
    int capability;
    double* data;
    int index;
    int size;
    double sum;
    double mean;
    double stddev;
    double mean_prev;
    // double stddev_prev;
};

static double velocity = 0;
static double distance = 0;
static double accel = 0;

static double barometer_velocity = 0;
static double barometer_distance = 0;
static double barometer_pressure = 0;
static double barometer_begin = 0;
static double barometer_end = 0;

static double barometer_height_discontinuous = 0;

struct accelerometer_motion {
    struct stream* stream;
    double height;    // --> physical height
    double distance;  // current running distance, maybe reset to zero when running finished
    double velocity;  // velocity, +-

    enum motion_state state;
} _accelerometer_motion;

struct barometer_motion {
    struct stream* stream;
    double height;
    double distance;
    double velocity;

    struct moving_window* mw;
    int64_t delay_stop_ts_ns;
} _barometer_motion;

static struct moving_window* _accel_moving_w = NULL;

static int notify_observer(enum observer_action action, void* data);

struct moving_window* moving_window_init(int size) {
    // data is append at end of struct moving_avg_window
    // make sure it's align on 4 bytes
    int ss = ((sizeof(struct moving_window) + 3) / 4) * 4;
    struct moving_window* w = (struct moving_window*)calloc(1, ss + size * sizeof(double));
    if (!w) {
        return NULL;
    }
    w->capability = size;
    w->data = (double*)((char*)w + ss);

    w->stddev = NAN;
    // w->stddev_prev = NAN;
    return w;
}

static int moving_window_update(struct moving_window* w, double val) {
    double var_sum = 0.0;

    if (!w) {
        return -1;
    }

    // window full
    // remove old value from sum
    if (w->size == w->capability) {
        w->sum -= w->data[w->index];
    }
    w->data[w->index] = val;
    w->sum += val;
    w->index = (w->index + 1) % w->capability;  // circle buffer
    if (w->size != w->capability) {
        w->size++;
    }

    if (w->size != w->capability) {
        return -1;  // not full window
    }

    w->mean_prev = w->mean;

    w->mean = w->sum / w->size;
    // HR_LOGD("capability:%d, index:%d, size:%d, mean:%f :\n", w->capability, w->index, w->size, w->mean);
    for (int i = 0; i < w->size; i++) {
        // HR_LOGD("%f", w->data[i]);
        // if (i != w->size - 1) {
        //     HR_LOGD(" ");
        // }
        var_sum += (w->data[i] - w->mean) * (w->data[i] - w->mean);
    }
    // HR_LOGD("\n");

    w->stddev = sqrt(var_sum / w->size);
    return 0;
}

static int moving_window_trim_avg(struct moving_window* w, double* val) {
    double max = -DBL_MAX;
    double min = DBL_MAX;
    if (w->size < 3) {
        return -1;
    }

    for (int i = 0; i < w->size; ++i) {
        double val = w->data[i];
        if (val > max) {
            max = val;
        }
        if (val < min) {
            min = val;
        }
    }

    *val = (w->sum - max - min) / (w->size - 2);

    return 0;
}

static int moving_window_deinit(struct moving_window* w) {
    if (!w) {
        return -1;
    }

    free(w);
    return 0;
}
int moving_window_is_stable(struct moving_window* w, double val) {
    if (moving_window_update(w, val) != 0) {
        return JITTER_UNKNOWN;
    }

    HR_LOGD("stddev:%f\n", w->stddev);
    if (w->stddev < ACCEL_JITTER_STD_THRESHOLD) {
        return JITTER_STABLE;
    }

    return JITTER_UNSTABLE;
}

static int MOVEMENT_FRAME_COUNT = 30;

const char* motion_state_str(enum motion_state state) {
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
}

double calculate_height_difference(double pressure1, double pressure2, double temperature) {
    static double fac = PRESSURE_L * PRESSURE_R / PRESSURE_M / G0;
    return ((temperature + 273.15) / L) * (1 - pow(pressure2 / pressure1, fac /*0.190284*/));
}
static void* _accelerometer_thread_routin(void* args) {
    char buf[MAX_LINE_LENGTH] = {0};
    int over_threshold_count = 0;
    int64_t delta_time_ns = seconds_to_nanoseconds(1) / ACCELEROMETER_SAMPLE_RATE_HZ;
    MOVEMENT_FRAME_COUNT = ACCELEROMETER_SAMPLE_RATE_HZ / 10;
    //
    ElevatorState state = ELEVATOR_STOPPED;
    ElevatorState state_pending = ELEVATOR_UNKNOWN;

    int floor_num = 0;
    char floor_label[64] = {0};

    if (!_accel_moving_w) {
        HR_LOGE("error: can not init moving avg window\n");
        return NULL;
    }

#if DUMP_DATA_TO_FILE
    if (_dump_fp) {
        snprintf(buf, sizeof(buf), "now,accel,velocity,distance,height,baro_height\n");
        fwrite(buf, 1, strlen(buf), _dump_fp);
    }
#endif

    struct stream* input = _accelerometer_motion.stream;
    double result[4] = {0};
    for (;;) {
        struct timespec spec;
        int64_t now = get_monotonic_nanoseconds();

        int ret = input->runonce(input, (void*)result, ARRAY_SIZE(result));
        if (ret != 0) {
            // error or calibration not complete
            if (ret == -2) {
                HR_LOGD("accelerometer is under calibration\n");
            }
            goto next_iteration;
        }

        double accel = result[0];
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

                struct motion_data md = {
                    .state = new_state,
                    .distance = distance};

                notify_observer(OBSERVER_ACTION_ON_MOTION, &md);
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
                struct motion_data md = {
                    .state = new_state,
                    .distance = distance};

                notify_observer(OBSERVER_ACTION_ON_MOTION, &md);
            }
            _accelerometer_motion.state = new_state;
        }

        floor_predict(_accelerometer_motion.height + _accelerometer_motion.distance, &floor_num, (char*)&floor_label, sizeof(floor_label));
        HR_LOGD("accel:%f, velocity:%f, distance:%f, height:%f\n",
                accel, velocity, distance, _accelerometer_motion.height + _accelerometer_motion.distance);
#if DUMP_DATA_TO_FILE
        if (_dump_fp) {
            snprintf(buf, sizeof(buf), "%lf,%f,%f,%f,%f,%f\n", (double)now / 1000000000.0, accel, velocity, distance, _accelerometer_motion.height + _accelerometer_motion.distance, barometer_distance);
            fwrite(buf, 1, strlen(buf), _dump_fp);
        }
#endif

        struct status_data stat = {.accel = accel, .speed = fabs(velocity), .distance = distance, .height = _accelerometer_motion.height + _accelerometer_motion.distance, .floor = atoi(floor_label), .running = (new_state != STOPPED)};
        notify_observer(OBSERVER_ACTION_ON_STATUS, &stat);

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
    int over_threshold_count = 0;
    int64_t delta_time_ns = seconds_to_nanoseconds(1) / BAROMETER_SAMPLE_RATE_HZ;

    int floor_num = 0;
    char floor_label[64] = {0};

#if 1
    if (!_barometer_motion.mw) {
        HR_LOGE("error: can not init moving avg window\n");
        return NULL;
    }
#endif

    struct stream* input = _barometer_motion.stream;
    double result[4] = {0};  // {pressure, temp}

    enum motion_state prev_state = STOPPED;
    double prev_pressure = 0;

    while (1) {
        struct timespec spec;
        int64_t now = get_monotonic_nanoseconds();

        int ret = input->runonce(input, (void*)result, ARRAY_SIZE(result));
        if (ret != 0) {
            // error or calibration not complete
            if (ret == -2) {
                HR_LOGD("barometer is under calibration\n");
            }
            goto next_iteration;
        }

        double pressure = result[0];
        double temp = result[1];
        // double velocity = round(result[1] * 100) / 100;
        // double distance = round(result[2] * 1000) / 1000;

        moving_window_update(_barometer_motion.mw, pressure);
        HR_LOGD("%s(%d): now:%ld, pressure:%f\n", __FUNCTION__, __LINE__, now, pressure);
        if (prev_pressure == 0) {
            prev_pressure = pressure;
        }

        // detect jump
        if (fabs(pressure - prev_pressure) > 50) {
            barometer_height_discontinuous += calculate_height_difference(prev_pressure, pressure, temp);
            HR_LOGD("%s(%d): mxp jump ... now:%ld, pressure:%f, previous:%f, height:%f\n",
                    __FUNCTION__, __LINE__,
                    now, pressure, prev_pressure, barometer_height_discontinuous);
        }

        prev_pressure = pressure;

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

int core_initalize(int argc, char** argv) {
#if DUMP_DATA_TO_FILE
    dump_data_init();
#endif

    memset((void*)&_accelerometer_motion, 0, sizeof(_accelerometer_motion));

    _accelerometer_motion.stream = accelerometer_motion_stream_init(ACCELEROMETER_SAMPLE_RATE_HZ);
    // simulate data, initialize floor
    // _accelerometer_motion.height = -6.1;

    if (!_accelerometer_motion.stream) {
        HR_LOGE("can not find accelerometer ...\n");
        return -1;
    }

    // which floor are we current stopping at?
    // should update height ?
    // or we should force wait base floor trigger
    _accelerometer_motion.stream->enter_calibration(_accelerometer_motion.stream);

    _accel_moving_w = moving_window_init(ACCELEROMETER_SAMPLE_RATE_HZ / 2);
    if (!_accel_moving_w) {
        return -1;
    }

    _barometer_motion.stream = barometer_motion_stream_init(BAROMETER_SAMPLE_RATE_HZ);
    if (!_barometer_motion.stream) {
        HR_LOGE("can not find barometer ...\n");
        return -1;
    }

    _barometer_motion.mw = moving_window_init(BAROMETER_SAMPLE_RATE_HZ * BAROMETER_WINDOW_DELAY_SECONDS);
    if (!_barometer_motion.mw) {
        moving_window_deinit(_accel_moving_w);
        return -1;
    }

    // acceleration_initialize();
    // barometer_initialize();
    return 0;
}

static int core_acceleration_start(void) {
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
    ret = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (0 != ret) {
        HR_LOGE("%s(%d): failed to pthread_attr_setdetachstate\n", __FUNCTION__, __LINE__);
        return -1;
    }

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

static int core_barometer_start(void) {
    int ret = 0;
    pthread_attr_t attr;
    struct sched_param param;
    int thread_policy;
    const int algorithm = SCHED_FIFO;  // SCHED_RR

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

int core_run(void) {
    core_barometer_start();
    core_acceleration_start();
    // wait device still

    // core_barometer_start();
    //  HR_LOGD("now device is ready ...\n");

    return 0;
}

int core_register_observer(struct core_observer* observer) {
    int i = 0;

    for (i = 0; i < ARRAY_SIZE(_observers); i++) {
        struct core_observer* obs = _observers[i];
        if (!obs) {
            _observers[i] = observer;
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

static int notify_observer(enum observer_action action, void* data) {
    int i = 0;

    for (i = 0; i < ARRAY_SIZE(_observers); i++) {
        struct core_observer* obs = _observers[i];
        if (obs) {
            switch (action) {
                case OBSERVER_ACTION_ON_STATUS:
                    if (obs->on_status) {
                        obs->on_status((struct status_data*)data);
                    }
                    break;
                case OBSERVER_ACTION_ON_MOTION:
                    if (obs->on_motion) {
                        obs->on_motion((struct motion_data*)data);
                    }
                    break;
                default:
                    break;
            }
        }
    }
    return 0;
}
