#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "acceleration.h"
#include "barometer.h"
#include "hr_log.h"

#if USE_LOCAL_SIMULATE_DATA
#include "simulate.h"
#endif

static int ACCEL_SAMPLE_RATE_HZ = 50;
static pthread_t _accel_tid = 0;

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

#define ACCEL_JITTER_STD_THRESHOLD 0.05

struct moving_avg_window {
    int capability;
    double *data;
    int index;
    int size;
    double sum;
    double mean;
    double stddev;
};

struct moving_avg_window *moving_average_window_init(int size) {
    // data is append at end of struct moving_avg_window
    // make sure it's align on 4 bytes
    int ss = ((sizeof(struct moving_avg_window) + 3) / 4) * 4;
    struct moving_avg_window *w = (struct moving_avg_window *)calloc(1, ss + size * sizeof(double));
    if (!w) return NULL;
    w->capability = size;
    w->data = (double *)((char *)w + ss);
    return w;
}

static int moving_window_stddev(struct moving_avg_window *w, double val, double *stddev) {
    double var_sum = 0.0;

    if (!w || !stddev)
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
        return -1; // not full window
    }
    w->mean = w->sum / w->size;
    printf("capability:%d, index:%d, size:%d, mean:%f :\n", w->capability, w->index, w->size, w->mean);
    for (int i = 0; i < w->size; i++) {
        printf("%f", w->data[i]);
        if (i != w->size - 1) {
            printf(" ");
        }
        var_sum += (w->data[i] - w->mean) * (w->data[i] - w->mean);
    }
    printf("\n");

    *stddev = sqrt(var_sum / w->size);
    w->stddev = *stddev;
    return 0;
}

int moving_window_is_stable(struct moving_avg_window *w, double val) {
    double stddev = 0;
    if (moving_window_stddev(w, val, &stddev) != 0) {
        return JITTER_UNKNOWN;
    }

    printf("stddev:%f\n", stddev);
    if (stddev < ACCEL_JITTER_STD_THRESHOLD) {
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

static void *_realtime_routin(void *args) {
    char buf[MAX_LINE_LENGTH] = {0};
    struct simulate_data data;
    int64_t delta_time_ns = 0;  // seconds_to_nanoseconds(1) / ACCEL_SAMPLE_RATE_HZ;
    struct moving_avg_window *w = moving_average_window_init(ACCEL_SAMPLE_RATE_HZ/2);
    if (!w) {
        printf("error: can not init moving avg window\n");
        return NULL;
    }

#if DUMP_DATA_TO_FILE
    if (_dump_fp) {
        snprintf(buf, sizeof(buf), "now,accel,pressure,temp,mean,stddev\n");
        fwrite(buf, 1, strlen(buf), _dump_fp);
    }
#endif

    for (;;) {
        struct timespec spec;
        int64_t now = system_mono_time_nanoseconds();

        if (simulate_data_read(&data) != 0) {
            break;
        }

        double stddev = 0;
        moving_window_stddev(w, data.accel_z, &stddev);
#if DUMP_DATA_TO_FILE
        if (_dump_fp) {
            snprintf(buf, sizeof(buf), "%f,%f,%f,%f,%f,%f\n", data.now, data.accel_z, data.pressure, data.temp, w->mean, stddev);

            fwrite(buf, 1, strlen(buf), _dump_fp);
        }
#endif
        //HR_LOGD("now:%ld, a:%f, stddev:%f, mean:%f\n", now, data.accel_z, stddev, w->mean);
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

int core_initalize(void) {
#if USE_LOCAL_SIMULATE_DATA
    if (simulate_data_init() != 0) {
        printf("simulate data init failed\n");
        return -1;
    }
#endif
#if DUMP_DATA_TO_FILE
    dump_data_init();
#endif

    acceleration_initialize();
    barometer_initialize();
    return 0;
}

int core_run(void) {
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

    ret = pthread_create(&_accel_tid, &attr, _realtime_routin, NULL);
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
}