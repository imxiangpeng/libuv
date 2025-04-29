#include "accelerometer_stream.h"

#include <assert.h>
#include <math.h>
#include <stddef.h>

#include <fftw3.h>
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

// force cut 10Hz
#define BUTTERWORTH_CUTOFF_FREQUENCY 10

#define EKF_N 4  // only accel
#define EKF_M 2  // only accel

#define _float_t double
#include "tinyekf.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define container_of(ptr, type, member) ({            \
    const typeof(((type*)0)->member)* __mptr = (ptr); \
    (type*)((char*)__mptr - offsetof(type, member));  \
})

struct fft_stream {
    size_t sampling_size;
    size_t count;
    double sum;
    // struct moving_window* mw;  // size 256, sampling
    double* in;
    fftw_complex* out;
    double jitter_frequency;
    double jitter_accel;
};

struct accelerometer_stream {
    struct motion_stream self;

    int sampling_frequency;
    struct sensor* sensor;
    struct butterworth_filter* filter[3];

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

    struct fft_stream fft[3];
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
static int _fft_process(struct accelerometer_stream* self, double* a, int len);

double hanning_window(int i, int N) {
    if (N <= 1) return 1.0;
    return 0.5 * (1.0 - cos(2.0 * M_PI * i / (N - 1)));
}
static void apply_hanning_window(struct fft_stream* f) {
    double mean = f->sum / f->sampling_size;
    if (f->sampling_size <= 1) {
        return;
    }

    for (size_t i = 0; i < f->sampling_size; i++) {
        double multiplier = 0.5 * (1.0 - cos(2.0 * M_PI * i / (f->sampling_size - 1)));
        f->in[i] -= mean;
        f->in[i] *= multiplier;
    }
}
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
    struct accelerometer_stream_data *p = (struct accelerometer_stream_data*)data;
    double accel_union = 0, accel_filter = 0;
    struct sensor_data_accelerometer accel;
    struct accelerometer_stream* s = container_of(self, struct accelerometer_stream, self);
    if (!self || !s) {
        return -1;
    }

    assert(sizeof(struct accelerometer_stream_data) == count);

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
        // accel_filter = butterworth_filter_process(s->bw_filter, accel_union);
    double accel_filtered[3] = {0};

    // butter worth filter cutoff 10hz
    for (size_t i = 0; i < ARRAY_SIZE(accel_filtered); i++) {
        accel_filtered[i] = butterworth_filter_process(s->filter[i], accel.x[i]);
    }

    _fft_process(s, accel_filtered, 3);

    // it indicates that the camera is inverted, when z < 0
    // accel_union = calculate_veritical_acceleration(accel.x[0], accel.x[1], accel.x[2]);
    accel_union = calculate_veritical_acceleration(accel_filtered[0], accel_filtered[1], accel_filtered[2]);
    if (s->inverted) {
        accel_union *= -1.0;
    }

    _ekf_run_model(s, accel_union, dt);

    s->distance = s->ekf.x[0];
    s->velocity = s->ekf.x[1];
#if 0    
    p[0] = s->ekf.x[2];
    p[1] = s->velocity;
    p[2] = s->distance;
    p[3] = s->ekf.x[2];
    p[4] = s->ekf.x[3];
    // add jitter freq & accel, using max magnitude's axis data
    p[5] = 0; // freq
    p[6] = 0; // accel
    for(size_t i = 0; i < ARRAY_SIZE(s->fft);i++) {
        if (s->fft[i].jitter_accel > p[6]) {
            p[6] = s->fft[i].jitter_accel;
            p[5] = s->fft[i].jitter_frequency;
        }
    }
#endif
    p->accel = s->ekf.x[2];
    p->velocity = s->ekf.x[1];
    p->distance = s->ekf.x[0];
    p->G = s->G;
    p->jitter_accel = s->fft[0].jitter_accel;
    p->jitter_frequency = s->fft[0].jitter_frequency;
    
    for(size_t i = 1; i < ARRAY_SIZE(s->fft);i++) {
        if (s->fft[i].jitter_accel > p->jitter_accel) {
            p->jitter_accel = s->fft[i].jitter_accel;
            p->jitter_frequency = s->fft[i].jitter_frequency;
        }
    }

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

    for (size_t i = 0; i < ARRAY_SIZE(s->filter); i++) {
        s->filter[i] = butterworth_filter_init(BUTTERWORTH_CUTOFF_FREQUENCY, sampling_frequency);
        if (!s->filter[i]) {
        }
    }

    s->mw = moving_window_init(sampling_frequency / 2);

    for (size_t i = 0; i < ARRAY_SIZE(s->fft); i++) {
        s->fft[i].count = 0;
        s->fft[i].sum = 0;
        s->fft[i].sampling_size = sampling_frequency;
        s->fft[i].in = (double*)fftw_malloc(sizeof(double) * s->fft[i].sampling_size);
        s->fft[i].out = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * (s->fft[i].sampling_size / 2 + 1));
    }
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

static int _fft_process(struct accelerometer_stream* self, double* a, int len) {
    if (!self || !a || len != 3) {
        return -1;
    }

    for (size_t i = 0; i < ARRAY_SIZE(self->fft); i++) {
        struct fft_stream* f = &self->fft[i];
        f->in[f->count] = a[i];
        f->sum += a[i];
        f->count++;

        if (f->count == f->sampling_size) {
            double mean = f->sum / f->sampling_size;
            

            double window_sum = 0.0;
            for ( size_t j = 0; j < f->sampling_size; j++) {
                double window_val = hanning_window(j, f->sampling_size);
                f->in[j] = (f->in[j] - mean) * window_val;
                window_sum += window_val;
            }
            // apply_hanning_window(f);
            f->count = 0;
            f->sum = 0;


            fftw_plan plan = fftw_plan_dft_r2c_1d(f->sampling_size, f->in, f->out, FFTW_ESTIMATE);
            fftw_execute(plan);
            fftw_destroy_plan(plan);

            int max_index = -1;
            double max_magnitude = 0.0;
            // reuse in buffer
            double* magnitudes = (double*)malloc(sizeof(double) * (f->sampling_size / 2 + 1));

            int N_fft_out = f->sampling_size / 2 + 1;
            for (int j = 0; j < N_fft_out; j++) {
                double real = f->out[j][0];
                double imag = f->out[j][1];
                double magnitude = sqrt(real * real + imag * imag);
                magnitudes[j] = magnitude;

                if (magnitude > max_magnitude) {
                    max_magnitude = magnitude;
                    max_index = j;
                }
            }

            free(magnitudes);
            magnitudes = NULL;

            double frequency = (double)max_index * self->sampling_frequency / f->sampling_size;
            double accel_value = (2.0 * max_magnitude) /  window_sum;//f->sampling_size;
            HR_LOGE("aix:%d: frequency:%f, accel_value:%f(max_magnitude:%f), mean:%f\n", i, frequency, accel_value, max_magnitude, mean);
            f->jitter_frequency = frequency;
            f->jitter_accel = accel_value;
            /*if (frequency == 0) {
                for (size_t j = 0; j < f->sampling_size; j++) {
                    HR_LOGD("%s(%d): %d -> %f\n", __FUNCTION__, __LINE__, j, f->in[j]);
                }
            }*/
        }
    }

    return 0;
}