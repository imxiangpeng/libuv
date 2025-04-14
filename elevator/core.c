#include <errno.h>
#include <float.h>
#include <math.h>
// #include <ncurses.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "acceleration.h"
#include "barometer.h"
#include "core.h"
#include "hr_log.h"

#if USE_LOCAL_SIMULATE_DATA
#include "simulate.h"
#endif

// 海平面标准气压 (Pa)
#define P0 101325.0

// 温度递减率 (K/m)
#define L 0.0065

static const double PRESSURE_L = 0.0065;
static const double PRESSURE_R = 8.31432;
static const double PRESSURE_M = 0.0289644;

static const double G0 = 9.823;

static struct core_observer *_sensor_observers[_SENSOR_MAX][10] = {{0}, {0}};
static int ACCEL_SAMPLE_RATE_HZ = 100;
static int BAROMETER_SAMPLE_RATE_HZ = 10;

static double MOVEMENT_THRESHOLD = 0.1f;
static double VELOCITY_ZUPT_THRESHOLD = 0.1f;

static pthread_t _accel_tid = 0;

static double _G = 9.823;

#define MAX_LINE_LENGTH 1000

#define DUMP_DATA_TO_FILE 1
// simulate using local csv files
#if DUMP_DATA_TO_FILE
static FILE *_dump_fp = NULL;
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
    double *data;
    int index;
    int size;
    double sum;
    double mean;
    double stddev;
    double mean_prev;
    double stddev_prev;
};

static double velocity = 0;
static double distance = 0;
static double accel = 0;

static double barometer_velocity = 0;
static double barometer_distance = 0;
static double barometer_pressure = 0;
static double barometer_begin = 0;
static double barometer_end = 0;

static struct moving_window *_accel_moving_w = NULL;
static struct moving_window *_barometer_moving_w = NULL;

struct moving_window *moving_window_init(int size) {
    // data is append at end of struct moving_avg_window
    // make sure it's align on 4 bytes
    int ss = ((sizeof(struct moving_window) + 3) / 4) * 4;
    struct moving_window *w = (struct moving_window *)calloc(1, ss + size * sizeof(double));
    if (!w) return NULL;
    w->capability = size;
    w->data = (double *)((char *)w + ss);

    w->stddev = NAN;
    w->stddev_prev = NAN;
    return w;
}

static int moving_window_update(struct moving_window *w, double val) {
    double var_sum = 0.0;

    if (!w)
        return -1;

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
    w->stddev_prev = w->stddev;

    w->mean = w->sum / w->size;
    // printf("capability:%d, index:%d, size:%d, mean:%f :\n", w->capability, w->index, w->size, w->mean);
    for (int i = 0; i < w->size; i++) {
        // printf("%f", w->data[i]);
        // if (i != w->size - 1) {
        //     printf(" ");
        // }
        var_sum += (w->data[i] - w->mean) * (w->data[i] - w->mean);
    }
    // printf("\n");

    w->stddev = sqrt(var_sum / w->size);
    return 0;
}

static int moving_window_trim_avg(struct moving_window *w, double *val) {
    double max = -DBL_MAX;
    double min = DBL_MAX;
    if (w->size < 3) return -1;

    for (int i = 0; i < w->size; ++i) {
        double val = w->data[i];
        if (val > max) max = val;
        if (val < min) min = val;
    }

    *val = (w->sum - max - min) / (w->size - 2);

    return 0;
}

static int moving_window_deinit(struct moving_window *w) {
    if (!w) return -1;

    free(w);
    return 0;
}
int moving_window_is_stable(struct moving_window *w, double val) {
    if (moving_window_update(w, val) != 0) {
        return JITTER_UNKNOWN;
    }

    printf("stddev:%f\n", w->stddev);
    if (w->stddev < ACCEL_JITTER_STD_THRESHOLD) {
        return JITTER_STABLE;
    }

    return JITTER_UNSTABLE;
}

static inline int64_t seconds_to_nanoseconds(int64_t secs) {
    return secs * 1000000000;
}

static int64_t system_mono_time_nanoseconds(void) {
    struct timespec t;
    t.tv_sec = t.tv_nsec = 0;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000000000LL + t.tv_nsec;
}
static int MOVEMENT_FRAME_COUNT = 30;

const char *state_to_str(int state) {
    switch (state) {
        case ELEVATOR_UNKNOWN:
            return "unknown";
        case ELEVATOR_STOPPED:
            return "stopped";
        case ELEVATOR_STARTING:
            return "starting";
        case ELEVATOR_SLOWING:
            return "slowing";
    }
    return "";
}

struct ncurses_data {
    double accel;
    double velocity;
    double distance;
};

static int notify_observers(enum core_sensor type, void *data) {
    int i = 0;

    for (i = 0; i < sizeof(_sensor_observers[type]) / sizeof(struct core_observer *); i++) {
        struct core_observer *obs = _sensor_observers[type][i];
        if (obs) {
            obs->update(type, data);
        }
    }
    return 0;
}

double calculate_height_difference(double pressure1, double pressure2, double temperature) {
    static double fac = PRESSURE_L * PRESSURE_R / PRESSURE_M / G0;
    return ((temperature + 273.15) / L) * (1 - pow(pressure2 / pressure1, fac /*0.190284*/));
}
static void *_accel_thread_routin(void *args) {
    char buf[MAX_LINE_LENGTH] = {0};
    int over_threshold_count = 0;
    int64_t delta_time_ns = seconds_to_nanoseconds(1) / ACCEL_SAMPLE_RATE_HZ;
    MOVEMENT_FRAME_COUNT = ACCEL_SAMPLE_RATE_HZ / 10;
    //
    ElevatorState state = ELEVATOR_STOPPED;
    ElevatorState state_pending = ELEVATOR_UNKNOWN;

    if (!_accel_moving_w) {
        printf("error: can not init moving avg window\n");
        return NULL;
    }

    if (!_barometer_moving_w) {
        printf("error: can not init moving avg window\n");
        return NULL;
    }
#if DUMP_DATA_TO_FILE
    if (_dump_fp) {
        snprintf(buf, sizeof(buf), "now,accel,pressure,temp,mean,stddev,v,d,v2,d2\n");
        fwrite(buf, 1, strlen(buf), _dump_fp);
    }
#endif

    // _G = -9.823;
    int l = 0;
    for (;;) {
        struct timespec spec;
        int64_t now = system_mono_time_nanoseconds();

#if USE_LOCAL_SIMULATE_DATA
        struct simulate_data data;
        if (simulate_data_read(&data) != 0) {
            break;
        }

        l++;

        int ret = moving_window_update(_accel_moving_w, data.accel_z);
        if (ret != 0 || _accel_moving_w->mean == 0)
            continue;

        ret = moving_window_update(_barometer_moving_w, data.pressure);

        double old_pressure = barometer_pressure;
        ret = moving_window_trim_avg(_barometer_moving_w, &barometer_pressure);

        // printf("_elevator_state:%d\n", _elevator_state);
        if (ret == 0 && old_pressure != 0 && barometer_pressure != 0) {
            double d = calculate_height_difference(barometer_pressure, old_pressure, data.temp);
            barometer_distance += d;
            barometer_velocity = d * 1000000000 / delta_time_ns;
            // printf("old %f vs %f(%f) -> barometer distance:%f(%f),velocity:%f\n", old_pressure, barometer_pressure, data.pressure, barometer_distance, d, barometer_velocity);
        }
        // window is full ...
        // if (fabs(_accel_moving_w->data[_accel_moving_w->index] - _G) > 0.2 && _accel_moving_w->stddev > 0.03) {
        //       printf("begin .......................\n");
        // }
        // 静止或者匀速,开始运动或者结束了
        // printf("fabs(_accel_moving_w->data[_accel_moving_w->index] - _G) :%f,%f\n", fabs(_accel_moving_w->data[_accel_moving_w->index] - _G) , _accel_moving_w->stddev);
        if (fabs(_accel_moving_w->data[_accel_moving_w->index] - _G) < 0.09 && _accel_moving_w->stddev < 0.03) {
            if (fabs(velocity) > VELOCITY_ZUPT_THRESHOLD) {
                // printf("velocity ....:%f\n", velocity);
                distance += velocity * data.dt;
                _elevator_state = ELEVATOR_CONSTANT;
            } else {
                // printf("not running ...\n");
                velocity = 0;
                // printf("ZUPT\n");
                _elevator_state = ELEVATOR_STOPPED;
                barometer_end = data.pressure;

                double high = calculate_height_difference(barometer_pressure, barometer_begin, data.temp);
                // printf("run finished: high:%f v %f\n", high, barometer_distance);
            }
        } else {
            // printf("running ....\n");
            // printf("old v:%f, d:%f, a:%f\n", velocity, distance, accel);
            distance += velocity * data.dt + 0.5 * accel * data.dt * data.dt;
            velocity += accel /*(_accel_moving_w->data[_accel_moving_w->index] - _G)*/ * data.dt;
            // printf("current v:%f, d:%f, a:%f, stddev:%f, dt:%f\n", velocity, distance, accel, _accel_moving_w->stddev, data.dt);
            if (velocity * accel > 0) {
                if (_elevator_state == ELEVATOR_STOPPED) {
                    barometer_begin = data.pressure;
                }
                _elevator_state = ELEVATOR_STARTING;
                // printf("speeding ..........\n");
            } else {
                // printf("slowing ......\n");
                _elevator_state = ELEVATOR_SLOWING;
            }
        }

        accel = _accel_moving_w->data[_accel_moving_w->index] - _G;
        if (fabs(accel) < 0.03) {
            accel = 0;
        }

        if (accel == 0) {
            if (fabs(velocity) < 0.3)
                velocity = 0;
        }
#if DUMP_DATA_TO_FILE
        if (_dump_fp) {
            snprintf(buf, sizeof(buf), "%f,%f,%f,%f,%f,%f,%f,%f,%f,%f\n", data.now, data.accel_z, data.pressure, data.temp, _accel_moving_w->mean, _accel_moving_w->stddev, velocity, distance, barometer_velocity, barometer_distance);
            fwrite(buf, 1, strlen(buf), _dump_fp);
        }
#endif
#endif

        struct live_stat stat = {.accel = accel, .speed = fabs(velocity), .distance = distance, .high = distance, .floor = 0, .pressure = data.pressure, .barometer_velocity = barometer_velocity, .barometer_distance = barometer_distance};
        notify_observers(SENSOR_ACCELERATION, &stat);

        // HR_LOGD("now:%ld, a:%f, stddev:%f, mean:%f\n", now, data.accel_z, stddev, w->mean);
        spec.tv_sec = (now + delta_time_ns) / 1000000000;
        spec.tv_nsec = (now + delta_time_ns) % 1000000000;
        int err;
        do {
            err = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &spec, NULL);
        } while (err < 0 && errno == EINTR);
    }

#if USE_LOCAL_SIMULATE_DATA
    simulate_data_deinit();
#endif

#if DUMP_DATA_TO_FILE
    if (_dump_fp) {
        fclose(_dump_fp);
        _dump_fp = NULL;
    }
#endif
    printf("finished ...\n");
    return NULL;
}

#if 0
static void _barometer_threadroutin(void *args) {
    for (;;) {
        struct timespec spec;
        int64_t now = system_mono_time_nanoseconds();

#if USE_LOCAL_SIMULATE_DATA
        struct simulate_data data;
        if (simulate_data_read(&data) != 0) {
            break;
        }

        int ret = moving_window_update(_accel_moving_w, data.accel_z);
        if (ret != 0 || _accel_moving_w->mean == 0)
            continue;        struct simulate_data data;
        if (simulate_data_read(&data) != 0) {
            break;
        

        // window is full ...
        // 静止或者匀速,开始运动或者结束了
        if (fabs(_accel_moving_w->data[_accel_moving_w->index] - _G) < 0.02 && _accel_moving_w->stddev < 0.03) {
            if (fabs(velocity) > VELOCITY_ZUPT_THRESHOLD) {
                // printf("velocity ....:%f\n", velocity);
                distance += velocity * data.dt;
            } else {
                // printf("not running ...\n");
                velocity = 0;
                // printf("ZUPT\n");
            }
        } else {
            // printf("running ....\n");
            // printf("old v:%f, d:%f, a:%f\n", velocity, distance, accel);
            distance += velocity * data.dt + 0.5 * accel * data.dt * data.dt;
            velocity += accel /*(_accel_moving_w->data[_accel_moving_w->index] - _G)*/ * data.dt;
            // printf("current v:%f, d:%f, a:%f, stddev:%f, dt:%f\n", velocity, distance, accel, _accel_moving_w->stddev, data.dt);
            if (velocity * accel > 0) {
                // printf("speeding ..........\n");
            } else {
                // printf("slowing ......\n");
            }
        }

        accel = _accel_moving_w->data[_accel_moving_w->index] - _G;

#if DUMP_DATA_TO_FILE
        if (_dump_fp) {
            snprintf(buf, sizeof(buf), "%f,%f,%f,%f,%f,%f,%f,%f\n", data.now, data.accel_z, data.pressure, data.temp, _accel_moving_w->mean, _accel_moving_w->stddev, velocity, distance);
            fwrite(buf, 1, strlen(buf), _dump_fp);
        }
#endif
#endif

        struct live_stat stat = {accel, fabs(velocity), distance, distance, 0};
        notify_observers(SENSOR_ACCELERATION, &stat);

        // HR_LOGD("now:%ld, a:%f, stddev:%f, mean:%f\n", now, data.accel_z, stddev, w->mean);
        spec.tv_sec = (now + delta_time_ns) / 1000000000;
        spec.tv_nsec = (now + delta_time_ns) % 1000000000;
        int err;
        do {
            err = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &spec, NULL);
        } while (err < 0 && errno == EINTR);
    }

#if USE_LOCAL_SIMULATE_DATA
    simulate_data_deinit();
#endif

#if DUMP_DATA_TO_FILE
    if (_dump_fp) {
        fclose(_dump_fp);
        _dump_fp = NULL;
    }
#endif
    printf("finished ...\n");
    return NULL;
}
#endif

#if DUMP_DATA_TO_FILE
int dump_data_init() {
    FILE *fp = fopen("result.csv", "w+");
    if (!fp) {
        perror("open error:");
        fclose(fp);
        return -1;
    }

    _dump_fp = fp;

    return 0;
}
#endif

int core_initalize(int argc, char **argv) {
#if USE_LOCAL_SIMULATE_DATA
    const char *path = NULL;
    if (argc > 1) {
        path = argv[1];
    }
    if (simulate_data_init(path) != 0) {
        printf("simulate data init failed\n");
        return -1;
    }
#endif
#if DUMP_DATA_TO_FILE
    dump_data_init();
#endif

    _accel_moving_w = moving_window_init(ACCEL_SAMPLE_RATE_HZ / 2);
    if (!_accel_moving_w) {
        return -1;
    }

    _barometer_moving_w = moving_window_init(BAROMETER_SAMPLE_RATE_HZ);
    if (!_barometer_moving_w) {
        moving_window_deinit(_accel_moving_w);
        return -1;
    }

    acceleration_initialize();
    barometer_initialize();

    return 0;
}

// mainly detect local G
static int core_acceleration_calibration(void) {
    int64_t delta_time_ns = seconds_to_nanoseconds(1) / ACCEL_SAMPLE_RATE_HZ;
    int calibration_retries_max = ACCEL_SAMPLE_RATE_HZ * 2;
    int calibration_retries = 0;
    double *calibration_data = (double *)calloc(sizeof(double), calibration_retries_max);
    if (!calibration_data)
        return -1;
    int l = 0;
    for (;;) {
        struct timespec spec;
        int64_t now = system_mono_time_nanoseconds();

        l++;
#if USE_LOCAL_SIMULATE_DATA
        struct simulate_data data;
        if (simulate_data_read(&data) != 0) {
            break;
        }

        int ret = moving_window_update(_accel_moving_w, data.accel_z);
        if (ret == 0 && !isnan(_accel_moving_w->stddev) /* && !isnan(_accel_moving_w->stddev_prev)*/) {
            // printf("stddev:%f\n", stddev);
            if (_accel_moving_w->stddev < ACCEL_JITTER_STD_THRESHOLD) {
                // fill from end to head
                calibration_data[calibration_retries] = _accel_moving_w->mean;
                // printf("%d -> %f\n", calibration_retries, calibration_data[calibration_retries]);

                calibration_retries++;
                //                printf("xx:%d\n", calibration_retries);
                if (calibration_retries == calibration_retries_max) {
                    calibration_retries = 0;
                    if (fabs(_accel_moving_w->mean - calibration_data[0]) < ACCEL_JITTER_STD_THRESHOLD) {
                        int i = 0;
                        double sum = 0;
                        for (i = 0; i < calibration_retries_max; i++) {
                            // printf("%f\n", calibration_data[i]);
                            sum += calibration_data[i];
                        }
                        printf("avg: -> %f\n", sum / calibration_retries_max);
                        _G = round(sum * 10000 / calibration_retries_max) / 10000;
                        printf("it's still: %lf, %d\n", _G, l);
                        break;
                    } else {
                        calibration_retries = 0;
                    }
                }
            } else {
                printf("not still:%d \n", l);
                calibration_retries = 0;
            }
        }
#endif
        // HR_LOGD("now:%ld, a:%f, stddev:%f, mean:%f\n", now, data.accel_z, stddev, w->mean);
    next_iteration:
        spec.tv_sec = (now + delta_time_ns) / 1000000000;
        spec.tv_nsec = (now + delta_time_ns) % 1000000000;
        int err;
        do {
            err = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &spec, NULL);
        } while (err < 0 && errno == EINTR);
    }

    free(calibration_data);
    calibration_data = NULL;

    return 0;
}

static int core_acceleration_start(void) {
    int ret = 0;
    pthread_attr_t attr;
    struct sched_param param;
    int thread_policy;
    const int algorithm = SCHED_FIFO;  // SCHED_RR

    if (_accel_tid != 0)
        return -1;

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
        printf("failed:%d ...\n", ret);
    }

    printf("max level:%d\n", param.sched_priority);
    ret = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (0 != ret) {
        HR_LOGE("%s(%d): failed to pthread_attr_setdetachstate\n", __FUNCTION__, __LINE__);
        return -1;
    }

    ret = pthread_create(&_accel_tid, &attr, _accel_thread_routin, NULL);
    if (0 != ret) {
        HR_LOGE("%s(%d): failed to pthread_create\n", __FUNCTION__, __LINE__);
        return -1;
    }

    ret = pthread_getschedparam(_accel_tid, &thread_policy, &param);
    if (ret != 0) {
        perror("pri:");
        printf("failed ...\n");
    }
    HR_LOGD("thread policy is %s, priority is %d\n",
            ((thread_policy == SCHED_FIFO) ? "FIFO" : (thread_policy == SCHED_RR ? "RR" : (thread_policy == SCHED_OTHER ? "OTHER" : "unknown"))), param.sched_priority);

    pthread_attr_destroy(&attr);

    return 0;
}
int core_run(void) {
    // wait device still
    // core_acceleration_calibration();
    // _G = 9.843f;
    _G = -9.823f;

    printf("now device is ready ...\n");

    core_acceleration_start();

    return 0;
}

int core_register_observer(enum core_sensor type, struct core_observer *observer) {
    int i = 0;
    int available = -1;

    for (i = 0; i < sizeof(_sensor_observers[type]) / sizeof(struct core_observer *); i++) {
        struct core_observer *obs = _sensor_observers[type][i];
        if (!obs) {
            if (available == -1) {
                available = i;
            }
        } else {
            if (obs == observer) {
                // already exists!
                return 0;
            }
        }
    }
    if (available == -1)
        return -1;
    _sensor_observers[type][available] = observer;
    return 0;
}