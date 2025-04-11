#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "acceleration.h"
#include "barometer.h"
#include "hr_log.h"

#if USE_LOCAL_SIMULATE_DATA
#include "simulate.h"
#endif

static int ACCEL_SAMPLE_RATE_HZ = 50;

static double MOVEMENT_THRESHOLD = 0.1f;

static pthread_t _accel_tid = 0;

static double _G = 9.81;

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

static struct moving_window *_accel_moving_w = NULL;

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

static void *_accel_thread_routin(void *args) {
    char buf[MAX_LINE_LENGTH] = {0};
    int over_threshold_count = 0;
    int64_t delta_time_ns = 0;  // seconds_to_nanoseconds(1) / ACCEL_SAMPLE_RATE_HZ;
    MOVEMENT_FRAME_COUNT = ACCEL_SAMPLE_RATE_HZ / 10;
    //
    ElevatorState state = ELEVATOR_STOPPED;
    ElevatorState state_pending = ELEVATOR_UNKNOWN;

    if (!_accel_moving_w) {
        printf("error: can not init moving avg window\n");
        return NULL;
    }

#if DUMP_DATA_TO_FILE
    if (_dump_fp) {
        snprintf(buf, sizeof(buf), "now,accel,pressure,temp,mean,stddev,v,d\n");
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
        if (ret != 0 || _accel_moving_w->mean_prev == 0)
            continue;

        // 静止或者匀速,开始运动或者结束了
        if (fabs(_accel_moving_w->data[_accel_moving_w->index] - _G) < 0.02 && _accel_moving_w->stddev < 0.03) {
            if (fabs(velocity) > 0.1) {
                printf("velocity ....:%f\n", velocity);
                distance += velocity * data.dt;
            } else {
                // printf("not running ...\n");
                velocity = 0;
                // printf("ZUPT\n");
            }
        } else {
            printf("running ....\n");
            distance += velocity * data.dt + 0.5 * accel * data.dt * data.dt;
            velocity += (_accel_moving_w->data[_accel_moving_w->index] - _G) * data.dt;
            printf("current v:%f, d:%f, a:%f, stddev:%f\n", velocity, distance, accel, _accel_moving_w->stddev);
            if (velocity * accel > 0) {
                printf("speeding ..........\n");
            } else {
                printf("slowing ......\n");
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
    for (;;) {
        struct timespec spec;
        int64_t now = system_mono_time_nanoseconds();

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
                        printf("it's still: %lf\n", _G);
                        break;
                    } else {
                        calibration_retries = 0;
                    }
                }
            } else {
                printf("not still \n");
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
    core_acceleration_calibration();

    printf("now device is ready ...\n");

    core_acceleration_start();
    

    return 0;
}