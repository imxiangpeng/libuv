#include <assert.h>
#include <math.h>
#include <stddef.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
// #include <tinyekf.h>
#include "butterworth_filter.h"
#include "hr_log.h"
#include "sensor.h"
#include "stream.h"
#include "time_utils.h"

#define EKF_N 4  // only accel
#define EKF_M 2  // only accel

#define _float_t double
#include "tinyekf.h"

#define container_of(ptr, type, member) ({            \
    const typeof(((type*)0)->member)* __mptr = (ptr); \
    (type*)((char*)__mptr - offsetof(type, member));  \
})

enum motion_state {
    MOTION_STATE_STOPPED = 0,
    MOTION_STATE_STARTING,
    MOTION_STATE_CONSTANT,
    MOTION_STATE_SLOWING,
};

struct moving_window {
    int capability;
    double* data;
    int index;
    int size;
    double sum;
    double mean;
    double stddev;
};

struct motion_stream {
    struct stream self;

    int sampling_frequency;
    struct sensor_device* sensor;
    struct filter* filter;
    struct butterworth_filter* bw_filter;

    double distance;
    double velocity;
    double accel;
    double high;

    double G;
    enum motion_state state;

    int calibration;
    int calibration_retries;
    int calibration_retries_max;
    double* calibration_data;
    ekf_t ekf;
    struct moving_window* mw;

    int64_t now;
};

static const _float_t pdiag[EKF_N] = {1.0, 1.0, 1.0, 1.0};

double Q[EKF_N * EKF_N] = {
    1e-1, 0, 0, 0,
    0, 1e-1, 0, 0,
    0, 0, 1e-1, 0,
    0, 0, 0, 1e-3};

static const double R[EKF_M * EKF_M] = {1e-1, 0,
                                        0, 1e-3};

static const double ACCEL_JITTER_STD_THRESHOLD = 0.03;
static const double G = 9.81;

static double _velocity = 0;
static double _distance = 0;

static double _bw_velocity = 0;
static double _bw_distance = 0;

static void _ekf_run_model(struct motion_stream* self, double input, double);

static struct moving_window* moving_window_init(int size);
static int moving_window_update(struct moving_window* w, double val);

static double calculate_veritical_acceleration(double x, double y, double z) {
    return sqrt(x * x + y * y + z * z) * (z < 0 ? -1 : 1);
}

static void calibration(struct motion_stream* m, double accel) {
    if (m->calibration != 0) {
        return;
    }

    int ret = moving_window_update(m->mw, accel);
    printf("%s(%d): ret:%d, stddev:%f, mean:%f, max:%d\n", __FUNCTION__, __LINE__, ret, m->mw->stddev, m->mw->mean, m->calibration_retries_max);
    if (ret == 0 && !isnan(m->mw->stddev)) {
        if (m->mw->stddev < ACCEL_JITTER_STD_THRESHOLD) {
            m->calibration_data[m->calibration_retries] = m->mw->mean;
            m->calibration_retries++;
            if (m->calibration_retries == m->calibration_retries_max) {
                int i = 0;
                double sum = 0;
                m->calibration_retries = 0;
                for (i = 0; i < m->calibration_retries_max; i++) {
                    printf("calibration: %f\n", m->calibration_data[i]);
                    sum += m->calibration_data[i];
                }
                printf("%s(%d):avg: -> %f\n", __FUNCTION__, __LINE__, sum / m->calibration_retries_max);
                m->G = round(sum * 10000 / m->calibration_retries_max) / 10000;
                printf("%s(%d): it's still: %lf\n", __FUNCTION__, __LINE__, m->G);
                m->calibration = 1;
                return;
            }
        } else {
            printf("not still:\n");
            m->calibration_retries = 0;
        }
    }
}
static int accelerometer_motion_stream_read(struct stream* stream, void* data, size_t count) {
    double dt = 0.01;
    int ret = -1;
    double* p = (double*)data;
    double accel_union = 0, accel_filter = 0;
    struct sensor_data_accelerometer accel;
    struct motion_stream* ms = container_of(stream, struct motion_stream, self);
    if (!stream || !ms) {
        return -1;
    }

    assert(count == 4);

    if (ms->now == 0) {
        dt = 0;
    } else {
        dt = ms->now;
        ms->now = get_monotonic_nanoseconds() / 1000000000.0f;
        dt = ms->now - dt;
    }

    ret = ms->sensor->read(&accel.self);
    if (ret != 0) {
        return -1;
    }

    // In our practical simulation comparison tests,
    // we found that the impact of dt is significant.
    // Therefore, it is crucial for us to use an accurate dt value.
    // For simulation environments, we allow the dt parameter to be passed.
#if USE_LOCAL_SIMULATE_DATA
    dt = accel.dt;
#endif

    printf("dt:%f\n", dt);
    accel_union = calculate_veritical_acceleration(accel.x[0], accel.x[1], accel.x[2]);

    accel_filter = butterworth_filter_process(ms->bw_filter, accel_union);

    _ekf_run_model(ms, accel_union, dt);

    ms->distance = ms->ekf.x[0];
    ms->velocity = ms->ekf.x[1];
    p[0] = ms->ekf.x[2];
    p[1] = ms->velocity;
    p[2] = ms->distance;
    p[3] = fabs(ms->ekf.x[2]);

    calibration(ms, ms->ekf.x[3]);
    HR_LOGD("%s(%d): union:%.3f vs filter:%.3f vs %.3f vs %.3f -- %.3f == %.3f\n",
            __FUNCTION__, __LINE__,
            accel_union, accel_filter, ms->ekf.x[2], ms->ekf.x[3], ms->G, ms->ekf.x[3] - ms->G);

    if (ms->calibration != 1) {
        return -2;  // we are calibration
    }

    _bw_distance += _bw_velocity * dt + 0.5 * (accel_filter - ms->G) * dt *dt;
    _bw_velocity += (accel_filter - ms->G) * dt;

    printf("bw filter: accel:%f, distance:%f, velocity:%f\n", accel_filter, _bw_distance, _bw_velocity);
    _distance += dt * _velocity + 0.5 * (accel_union - ms->G) * dt * dt;
    _velocity += dt * (accel_union - ms->G);
    HR_LOGD("manual distance & velocity: [%f, %f]\n", _distance, _velocity);
    return 0;
}

static int accelerometer_motion_stream_calibration_enter(struct stream* stream) {
    struct motion_stream* ms = container_of(stream, struct motion_stream, self);
    if (!stream || !ms) {
        return -1;
    }

    ms->calibration = 0;
    ms->calibration_retries = 0;
    return 0;
}
static int accelerometer_motion_stream_calibration_completed(struct stream* stream) {
    return 0;
}

static int accelerometer_motion_stream_reset(struct stream* stream) {
    printf("%s(%d): \n", __FUNCTION__, __LINE__);

    struct motion_stream* ms = container_of(stream, struct motion_stream, self);
    if (!stream || !ms) {
        return -1;
    }

    // clear distance & speed
    ms->ekf.x[0] = 0;
    ms->ekf.x[1] = 0;

    return 0;
}
static int accelerometer_motion_stream_close(struct stream* stream) {
    printf("%s(%d): \n", __FUNCTION__, __LINE__);
    return 0;
}

struct stream* accelerometer_motion_stream_init(int sampling_frequency) {
    int ret = 0;
    int i = 0;
    double accel_union = 0, accel_filter = 0;
    struct sensor_data_accelerometer accel;
    struct motion_stream* ms = (struct motion_stream*)calloc(1, sizeof(struct motion_stream));
    if (!ms) {
        return NULL;
    }

    ms->G = G;
    printf("%s(%d): motion stream:%p vs %p\n", __FUNCTION__, __LINE__, ms, &ms->self);
    ms->sampling_frequency = sampling_frequency;
    ms->self.enter_calibration = accelerometer_motion_stream_calibration_enter;
    ms->self.calibration_completed = accelerometer_motion_stream_calibration_completed;
    ms->self.runonce = accelerometer_motion_stream_read;
    ms->self.reset = accelerometer_motion_stream_reset;
    ms->self.close = accelerometer_motion_stream_close;

    ms->bw_filter = butterworth_filter_init(5, sampling_frequency);

    ms->mw = moving_window_init(sampling_frequency / 2);

    ekf_initialize(&ms->ekf, pdiag);

    ms->sensor = sensor_manager_get_device(SENSOR_ACCELEROMETER);

    if (0 != ms->sensor->init()) {
        free(ms);
        return NULL;
    }

    ms->sensor->configure(ms->sampling_frequency);

    ms->sensor->read(&accel.self);

    ms->G = calculate_veritical_acceleration(accel.x[0], accel.x[1], accel.x[2]);
    ms->ekf.x[3] = ms->G;

    ms->calibration = 0;
    ms->calibration_retries_max = ms->sampling_frequency * 2;
    ms->calibration_data = (double*)calloc(sizeof(double), ms->calibration_retries_max);
#if 0
    while (i++ < ms->sampling_frequency) {
        ret = ms->sensor->read(&accel.self);
        if (ret != 0) {
            free(ms);
            return NULL;
        }

        accel_union = sqrt(accel.x[0] * accel.x[0] + accel.x[1] * accel.x[1] + accel.x[2] * accel.x[2]);

        accel_filter = butterworth_filter_process(ms->bw_filter, accel_union);
        _ekf_run_model(ms, accel_union);
        usleep(1000 / ms->sampling_frequency);
    }
#endif

    return &ms->self;
}

static struct moving_window* moving_window_init(int size) {
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

static void _ekf_run_model(struct motion_stream* self, double input, double dt) {
    // double dt = 0.01;
    ekf_t* ekf = NULL;  //&self->ekf;
    double linear_accel = 0;
    if (!self) {
        return;
    }

    ekf = &self->ekf;
    // dt = 1.0 / self->sampling_frequency;

    linear_accel = input - self->G;

    // F_k
    double F[EKF_N * EKF_N] = {
        1, dt, 0.5 * dt * dt, 0,
        0, 1, dt, 0,
        0, 0, 1, 0,
        0, 0, 0, 1};

    // H_k
    const double H[EKF_M * EKF_N] = {
        0, 0, 1, 0,
        0, 0, 0, 1};

    // predict
    double fx[EKF_N] = {
        ekf->x[0] + ekf->x[1] * dt + 0.5 * ekf->x[2] * dt * dt,
        ekf->x[1] + ekf->x[2] * dt,
        ekf->x[2],
        ekf->x[3]};

    // only date accel
    if (self->calibration != 1) {
        F[0] = 0;
        F[1] = 0;
        F[2] = 0;
        F[EKF_N + 1] = 0;
        F[EKF_N + 2] = 0;

        fx[0] = 0;
        ekf->P[0] = 1e-6;
        fx[1] = 0;
        ekf->P[EKF_N + 1] = 1e-6;
        fx[2] = 0;
        ekf->P[2 * EKF_N + 2] = 1e-6;

        linear_accel = 0;
    }

    printf("a:%f, x:%f-%f-%f-%f\n", input, ekf->x[0], ekf->x[1], ekf->x[2], ekf->x[3]);

    if (self->calibration == 0 || fabs(ekf->x[1]) < 0.1 && fabs(linear_accel) < 0.09) {
        printf("ZUPT ...............\n");
        fx[1] = 0;
        ekf->x[1] = 0;             // 速度置 0
        ekf->P[EKF_N + 1] = 1e-6;  // 速度误差极小，避免恢复
        // Q[ EKF_N + 1] = 1e-6;  // 降低速度噪声
        // F[1] = 0;
        // F[EKF_N + 1] = 0;
        _velocity = 0;
    }

    HR_LOGD("fx: [%f, %f, %f,%f]\n", fx[0], fx[1], fx[2], fx[3]);
    HR_LOGD("x: [%f, %f, %f,%f]\n", ekf->x[0], ekf->x[1], ekf->x[2], ekf->x[3]);
    ekf_predict(ekf, fx, F, Q);

    if (fabs(linear_accel) < 0.03) {
        linear_accel = 0;
    }

    const double z[EKF_M] = {linear_accel, input};

    const double hx[EKF_M] = {ekf->x[2], ekf->x[3]};

    HR_LOGD("z: [%f, %f]\n", z[0], z[1]);
    HR_LOGD("hx: [%f, %f]\n", hx[0], hx[1]);
    ekf_update(ekf, z, hx, H, R);
    HR_LOGD("after x: [%f, %f, %f,%f]\n", ekf->x[0], ekf->x[1], ekf->x[2], ekf->x[3]);

    //_distance += dt * _velocity + 0.5 * linear_accel * dt *dt;
    //_velocity += dt * linear_accel;
    // HR_LOGD("manual distance & velocity: [%f, %f]\n", _distance, _velocity);
}
