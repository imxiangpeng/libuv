#include "accelerometer_stream.h"

#include <assert.h>
#include <math.h>
#include <stddef.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "butterworth_filter.h"
#include "hr_log.h"
#include "motion_stream.h"
#include "moving_window.h"
#include "sensor.h"
#include "time_utils.h"

#define EKF_N 4  // only accel
#define EKF_M 2  // only accel

#define _float_t double
#include "tinyekf.h"

#define container_of(ptr, type, member) ({            \
    const typeof(((type*)0)->member)* __mptr = (ptr); \
    (type*)((char*)__mptr - offsetof(type, member));  \
})

struct accelerometer_stream {
    struct motion_stream self;

    int sampling_frequency;
    struct sensor* sensor;
    // struct butterworth_filter* bw_filter;

    double distance;
    double velocity;
    double accel;
    double high;

    double G;
    int inverted;

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

static const double R[EKF_M * EKF_M] = {1e-1, 0, 0, 1e-3};

static const double ACCEL_JITTER_STD_THRESHOLD = 0.03;
static const double G = 9.81;

// static double _velocity = 0;
// static double _distance = 0;

// static double _bw_velocity = 0;
// static double _bw_distance = 0;

static void _ekf_run_model(struct accelerometer_stream* self, double input, double dt);

static double calculate_veritical_acceleration(double x, double y, double z) {
    return sqrt(x * x + y * y + z * z) * (z < 0 ? -1 : 1);
}

static void calibration(struct accelerometer_stream* self, double accel) {
    if (self->calibration != 0) {
        return;
    }

    int ret = moving_window_update(self->mw, accel);
    HR_LOGD("%s(%d): ret:%d, stddev:%f, mean:%f, max:%d\n", __FUNCTION__, __LINE__, ret, self->mw->stddev, self->mw->mean, self->calibration_retries_max);
    if (ret == 0 && !isnan(self->mw->stddev)) {
        if (self->mw->stddev < ACCEL_JITTER_STD_THRESHOLD) {
            self->calibration_data[self->calibration_retries] = self->mw->mean;
            self->calibration_retries++;
            if (self->calibration_retries == self->calibration_retries_max) {
                int i = 0;
                double sum = 0;
                self->calibration_retries = 0;
                for (i = 0; i < self->calibration_retries_max; i++) {
                    HR_LOGD("calibration: %f\n", self->calibration_data[i]);
                    sum += self->calibration_data[i];
                }
                HR_LOGD("%s(%d):avg: -> %f\n", __FUNCTION__, __LINE__, sum / self->calibration_retries_max);
                self->G = round(sum * 10000 / self->calibration_retries_max) / 10000;
                HR_LOGD("%s(%d): it's still: %lf\n", __FUNCTION__, __LINE__, self->G);
                self->calibration = 1;
                return;
            }
        } else {
            HR_LOGD("not still:\n");
            self->calibration_retries = 0;
        }
    }
}

static int accelerometer_stream_open(struct motion_stream* self) {
    struct sensor_data_accelerometer accel;
    struct accelerometer_stream* s = container_of(self, struct accelerometer_stream, self);
    if (!self || !s) {
        return -1;
    }

    ekf_initialize(&s->ekf, pdiag);

    s->sensor = sensor_instance(SENSOR_ACCELEROMETER);

    if (0 != s->sensor->init()) {
        HR_LOGE("%s(%d): can open open accelerometer ...\n", __FUNCTION__, __LINE__);
        return -1;
    }

    s->sensor->configure(s->sampling_frequency);

    s->sensor->read(&accel.self);

    if (accel.x[2] < 0) {
        s->inverted = 1;
    }

    s->G = calculate_veritical_acceleration(accel.x[0], accel.x[1], accel.x[2]);
    if (s->inverted) {
        s->G *= -1.0;
    }

    // update ekf status
    s->ekf.x[3] = s->G;

    s->calibration = 0;
    s->calibration_retries_max = s->sampling_frequency * 2;
    s->calibration_data = (double*)calloc(sizeof(double), s->calibration_retries_max);

    return 0;
}
static int accelerometer_stream_read(struct motion_stream* self, void* data, size_t count) {
    double dt = 0.01;
    int ret = -1;
    double* p = (double*)data;
    double accel_union = 0, accel_filter = 0;
    struct sensor_data_accelerometer accel;
    struct accelerometer_stream* s = container_of(self, struct accelerometer_stream, self);
    if (!self || !s) {
        return -1;
    }

    assert(count >= 5);

    if (s->now == 0) {
        dt = 0;
        s->now = get_monotonic_nanoseconds();
    } else {
        int64_t prev = s->now;

        s->now = get_monotonic_nanoseconds();
        dt = (s->now - prev) / 1000000000.0;
    }

    ret = s->sensor->read(&accel.self);
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

    HR_LOGD("dt:%f\n", dt);
    // it indicates that the camera is inverted, when z < 0
    accel_union = calculate_veritical_acceleration(accel.x[0], accel.x[1], accel.x[2]);
    if (s->inverted) {
        accel_union *= -1.0;
    }
    // accel_filter = butterworth_filter_process(s->bw_filter, accel_union);

    _ekf_run_model(s, accel_union, dt);

    s->distance = s->ekf.x[0];
    s->velocity = s->ekf.x[1];
    p[0] = s->ekf.x[2];
    p[1] = s->velocity;
    p[2] = s->distance;
    p[3] = s->ekf.x[2];
    p[4] = s->ekf.x[3];

    calibration(s, s->ekf.x[3]);
    HR_LOGD("%s(%d): union:%.3f vs filter:%.3f vs %.3f vs %.3f -- %.3f == %.3f\n",
            __FUNCTION__, __LINE__,
            accel_union, accel_filter, s->ekf.x[2], s->ekf.x[3], s->G, s->ekf.x[3] - s->G);

    if (s->calibration != 1) {
        return -2;  // we are calibration
    }

    return 0;
}

static int accelerometer_stream_calibration_enter(struct motion_stream* self) {
    struct accelerometer_stream* s = container_of(self, struct accelerometer_stream, self);
    if (!self || !s) {
        return -1;
    }

    s->calibration = 0;
    s->calibration_retries = 0;
    return 0;
}
static int accelerometer_stream_calibration_completed(struct motion_stream* self) {
    struct accelerometer_stream* s = container_of(self, struct accelerometer_stream, self);
    if (!self || !s) {
        return -1;
    }

    return s->calibration;
}

static int accelerometer_stream_reset(struct motion_stream* self) {
    HR_LOGD("%s(%d): \n", __FUNCTION__, __LINE__);
    ekf_t* ekf = NULL;
    struct accelerometer_stream* ms = container_of(self, struct accelerometer_stream, self);
    if (!self || !ms) {
        return -1;
    }

    ekf = &ms->ekf;

    // clear distance & speed
    ms->ekf.x[0] = 0;
    ekf->P[0] = 1e-6;
    ms->ekf.x[1] = 0;
    ekf->P[EKF_N + 1] = 1e-6;

    return 0;
}

static int accelerometer_stream_close(struct motion_stream* self) {
    struct accelerometer_stream* s = container_of(self, struct accelerometer_stream, self);
    if (!self || !s) {
        return -1;
    }
    HR_LOGD("%s(%d): \n", __FUNCTION__, __LINE__);
    s->sensor->close();
    s->sensor = NULL;
    return 0;
}

struct motion_stream* accelerometer_stream_init(int sampling_frequency) {
    struct accelerometer_stream* s = (struct accelerometer_stream*)calloc(1, sizeof(struct accelerometer_stream));
    if (!s) {
        return NULL;
    }

    s->G = G;
    s->sampling_frequency = sampling_frequency;
    s->self.open = accelerometer_stream_open;
    s->self.enter_calibration = accelerometer_stream_calibration_enter;
    s->self.calibration_completed = accelerometer_stream_calibration_completed;
    s->self.read = accelerometer_stream_read;
    s->self.reset = accelerometer_stream_reset;
    s->self.close = accelerometer_stream_close;

    // s->bw_filter = butterworth_filter_init(5, sampling_frequency);

    s->mw = moving_window_init(sampling_frequency / 2);

    return &s->self;
}

int accelerometer_stream_deinit(struct motion_stream* self) {
    struct accelerometer_stream* s = container_of(self, struct accelerometer_stream, self);
    if (!self || !s) {
        return -1;
    }

    if (s->mw) {
        moving_window_release(s->mw);
        s->mw = NULL;
    }

    if (s->calibration_data) {
        free(s->calibration_data);
        s->calibration_data = NULL;
    }
    free(s);
    return 0;
}

static void _ekf_run_model(struct accelerometer_stream* self, double accel, double dt) {
    ekf_t* ekf = NULL;
    double linear_accel = 0;

    if (!self) {
        return;
    }

    ekf = &self->ekf;

    linear_accel = accel - self->G;

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

    HR_LOGD("a:%f, x:%f-%f-%f-%f\n", accel, ekf->x[0], ekf->x[1], ekf->x[2], ekf->x[3]);

    if (self->calibration == 0 || ((fabs(ekf->x[1]) != 0 && fabs(ekf->x[1]) < 0.1) && fabs(linear_accel) < 0.09)) {
        HR_LOGD("ZUPT ...............\n");
        fx[1] = 0;
        ekf->x[1] = 0;             // 速度置 0
        ekf->P[EKF_N + 1] = 1e-6;  // 速度误差极小，避免恢复
        // Q[ EKF_N + 1] = 1e-6;  // 降低速度噪声
        // F[1] = 0;
        // F[EKF_N + 1] = 0;
        // _velocity = 0;
    }

    // HR_LOGD("fx: [%f, %f, %f,%f]\n", fx[0], fx[1], fx[2], fx[3]);
    // HR_LOGD("x: [%f, %f, %f,%f]\n", ekf->x[0], ekf->x[1], ekf->x[2], ekf->x[3]);
    ekf_predict(ekf, fx, F, Q);

    if (fabs(linear_accel) < 0.05) {
        linear_accel = 0;
    }

    const double z[EKF_M] = {linear_accel, accel};

    const double hx[EKF_M] = {ekf->x[2], ekf->x[3]};

    // HR_LOGD("z: [%f, %f]\n", z[0], z[1]);
    // HR_LOGD("hx: [%f, %f]\n", hx[0], hx[1]);
    ekf_update(ekf, z, hx, H, R);
    // HR_LOGD("after x: [%f, %f, %f,%f]\n", ekf->x[0], ekf->x[1], ekf->x[2], ekf->x[3]);

    //_distance += dt * _velocity + 0.5 * linear_accel * dt *dt;
    //_velocity += dt * linear_accel;
    // HR_LOGD("manual distance & velocity: [%f, %f]\n", _distance, _velocity);
}
