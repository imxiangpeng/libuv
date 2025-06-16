// mxp, 202504, accelerometer stream, which can filter data and provide motion data

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
#include "sconf.h"
#include "sensor.h"
#include "time_utils.h"

// #define SENSOR_CALIBRATION_CONF "/etc/elevatord/sensor_calibration.conf"
#define SENSOR_CALIBRATION_CONF "imu_calibration.conf"

// reset when both acc & velocity below threshold
#define ZUPT_ACC_THRESHOLD 0.09
// 这里不能采用 0.1 不然会飞出去，出现无法归零的情况
#define ZUPT_SPEED_THRESHOLD 0.2

// force cut 10Hz
#define BUTTERWORTH_CUTOFF_FREQUENCY 10

#define ACCEL_JITTER_THRESHOLD 0.35

#define EKF_N 6  // [v,s,a,ax,ay,az]
#define EKF_M 4  // [a, ax, dy, ax]

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
    fftw_plan plan;
    double* in;
    fftw_complex* out;
    double jitter_frequency;
    double jitter_accel;
};

struct accelerometer_stream {
    struct motion_stream self;

    int sampling_frequency;
    struct sensor* sensor;
    struct butterworth_filter* filter[IMU_AXES];

    double distance;
    double velocity;
    double accel;
    double high;

    double G;
    int inverted;

    int is_calibration_completed;
    int calibration_retries;
    int calibration_retries_max;
    struct axis_mean {
        double x[IMU_AXES];
    }* calibration_data;
    ekf_t ekf;
    struct moving_window* calibration_mw[IMU_AXES];
    // should store
    double zero_bias_accels[IMU_AXES];
    double zero_bias_pitch;
    double zero_bias_roll;

    int64_t now;

    struct fft_stream fft[IMU_AXES];
};

enum calibration_field {
    E_CALIBRATED = 0,
    E_G_1000,
    E_BIAS_ACCEL_X_1000,
    E_BIAS_ACCEL_Y_1000,
    E_BIAS_ACCEL_Z_1000,
    E_PITCH_1000,
    E_ROLL_1000,
    E_FIELD_MAX
};
#if 0
static const char* const calibration_field_names[E_FIELD_MAX] = {
    "CALIBRATED",
    "G_1000",
    "BIAS_ACCEL_X_1000",
    "BIAS_ACCEL_Y_1000",
    "BIAS_ACCEL_Z_1000",
    "PITCH_1000",
    "ROLL_1000",
};

union calibration_data {
    struct {
        int64_t is_calibrated;
        int64_t g_1000;
        int64_t bias_accel_x_1000;
        int64_t bias_accel_y_1000;
        int64_t bias_accel_z_1000;
        int64_t pitch_1000;
        int64_t roll_1000;
    } field;
    int64_t arr[E_FIELD_MAX];
};
#endif
struct sconf_proto calibration_config[] = {
    [E_CALIBRATED] = {"CALIBRATED", PROTO_VALUE_INT64, {.int64 = 0}},
    [E_G_1000] = {"G_1000", PROTO_VALUE_NUMBER, {.number = 0}},
    [E_BIAS_ACCEL_X_1000] = {"BIAS_ACCEL_X_1000", PROTO_VALUE_NUMBER, {.number = 0}},
    [E_BIAS_ACCEL_Y_1000] = {"BIAS_ACCEL_Y_1000", PROTO_VALUE_NUMBER, {.number = 0}},
    [E_BIAS_ACCEL_Z_1000] = {"BIAS_ACCEL_Z_1000", PROTO_VALUE_NUMBER, {.number = 0}},
    [E_PITCH_1000] = {"PITCH_1000", PROTO_VALUE_NUMBER, {.number = 0}},
    [E_ROLL_1000] = {"ROLL_1000", PROTO_VALUE_NUMBER, {.number = 0}},
};

// clang-format off
static const _float_t pdiag[EKF_N] = {1.0, 1.0, 1.0, 1.0, 1.0, 1.0};

double Q[EKF_N * EKF_N] = {
    1e-1, 0, 0, 0, 0, 0,
    0, 1e-1, 0, 0, 0, 0,
    0, 0, 1e-3, 0, 0, 0,
    0, 0, 0, 1e-2, 0, 0,
    0, 0, 0, 0, 1e-2, 0,
    0, 0, 0, 0, 0, 1e-2,
};

static const double R[EKF_M * EKF_M] = {
    1e-1, 0, 0, 0,
    0, 1e-2, 0, 0,
    0, 0, 1e-2, 0,
    0, 0, 0, 1e-2,
};
// clang-format on

static const double ACCEL_JITTER_STD_THRESHOLD = 0.03;
static const double G = 9.81;

// static double _velocity = 0;
// static double _distance = 0;

// static double _bw_velocity = 0;
// static double _bw_distance = 0;

static void _ekf_run_model(struct accelerometer_stream* self, double input[IMU_AXES], double dt);
static int _fft_process(struct accelerometer_stream* self, double* a, int len);

static double hanning_window(int i, int N) {
    if (N <= 1)
        return 1.0;
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
static double calculate_stationary_veritical_acceleration(double x, double y, double z) {
    return sqrt(x * x + y * y + z * z) * (z < 0 ? -1 : 1);
}
static double calculate_veritical_acceleration(double x, double y, double z, double pitch, double roll) {
    return x * sin(pitch) - y * sin(roll) * cos(pitch) + z * cos(roll) * cos(pitch);
}

static void do_calibration_when_needed(struct accelerometer_stream* self, double accel[IMU_AXES]) {
    int ready = 1;
    if (self->is_calibration_completed != 0) {
        return;
    }

    for (size_t i = 0; i < ARRAY_SIZE(self->calibration_mw); i++) {
        int ret = moving_window_update(self->calibration_mw[i], accel[i]);
        HR_LOGD("%s(%d): ret:%d, stddev:%f, mean:%f, max:%d\n", __FUNCTION__, __LINE__, ret, self->calibration_mw[i]->stddev, self->calibration_mw[i]->mean, self->calibration_retries_max);
        if (ret != 0 || isnan(self->calibration_mw[i]->stddev)) {
            ready &= 0;
            continue;
        }
    }

    if (ready != 1) {
        self->calibration_retries = 0;
        return;
    }

    for (size_t i = 0; i < ARRAY_SIZE(self->calibration_mw); i++) {
        double slope = 0;
        moving_window_slope(self->calibration_mw[i], &slope);
        HR_LOGD("aixes:%d stddev:%f, cover:%f, slope:%f\n", i, self->calibration_mw[i]->stddev, self->calibration_mw[i]->stddev * self->calibration_mw[i]->stddev, slope);
        if (self->calibration_mw[i]->stddev >= ACCEL_JITTER_STD_THRESHOLD) {
            self->calibration_retries = 0;
            return;
        }
    }

    for (size_t i = 0; i < ARRAY_SIZE(self->calibration_mw); i++) {
        self->calibration_data[self->calibration_retries].x[i] = self->calibration_mw[i]->mean;
    }

    HR_LOGD("%s(%d): calibration retries:%d\n", __FUNCTION__, __LINE__, self->calibration_retries);
    self->calibration_retries++;
    if (self->calibration_retries == self->calibration_retries_max) {
        self->zero_bias_accels[0] = 0;
        self->zero_bias_accels[1] = 0;
        self->zero_bias_accels[2] = 0;
        for (int i = 0; i < self->calibration_retries_max; i++) {
            HR_LOGD("calibration: %f %f %f\n", self->calibration_data[i].x[0], self->calibration_data[i].x[1], self->calibration_data[i].x[2]);
            for (size_t j = 0; j < ARRAY_SIZE(self->calibration_mw); j++) {
                self->zero_bias_accels[j] += self->calibration_data[i].x[j];
            }
        }
        self->zero_bias_accels[0] /= self->calibration_retries_max;
        self->zero_bias_accels[1] /= self->calibration_retries_max;
        self->zero_bias_accels[2] /= self->calibration_retries_max;

        // self->zero_bias_accels[0] = round(self->zero_bias_accels[0] * 1000) / 1000;
        // self->zero_bias_accels[1] = round(self->zero_bias_accels[1] * 1000) / 1000;
        // self->zero_bias_accels[2] = round(self->zero_bias_accels[2] * 1000) / 1000;

        self->G = calculate_stationary_veritical_acceleration(self->zero_bias_accels[0], self->zero_bias_accels[1], self->zero_bias_accels[2]);

        self->G = round(self->G * 1000) / 1000;

        self->zero_bias_roll = atan2(-self->zero_bias_accels[1] /*y*/, self->zero_bias_accels[2] /*z*/);
        self->zero_bias_pitch = atan2(self->zero_bias_accels[0] /*x*/,
                                      sqrt(self->zero_bias_accels[0] * self->zero_bias_accels[0] + self->zero_bias_accels[1] * self->zero_bias_accels[1] + self->zero_bias_accels[2] * self->zero_bias_accels[2]));

        // self->zero_bias_pitch = round(self->zero_bias_pitch * 1000) / 1000;
        // self->zero_bias_roll = round(self->zero_bias_roll * 1000) / 1000;

        float az_world = self->zero_bias_accels[0] * sin(self->zero_bias_pitch) - self->zero_bias_accels[1] * sin(self->zero_bias_roll) * cos(self->zero_bias_pitch) + self->zero_bias_accels[2] * cos(self->zero_bias_roll) * cos(self->zero_bias_pitch);
        HR_LOGD("%s(%d) G:%f vs %f\n", __FUNCTION__, __LINE__, self->G, az_world);
        self->calibration_retries = 0;
        // self->G = round(sum * 10000 / self->calibration_retries_max) / 10000;
        HR_LOGD("%s(%d): it's still: %lf, zero offset: %f %f %f, pitch:%f, roll:%f\n", __FUNCTION__, __LINE__, self->G, self->zero_bias_accels[0], self->zero_bias_accels[1], self->zero_bias_accels[2], self->zero_bias_pitch, self->zero_bias_roll);

        self->is_calibration_completed = 1;
#if 0
        union calibration_data cdata;
        cdata.field.is_calibrated = 1;
        cdata.field.g_1000 = self->G * 1000;
        // cdata.field.bias_accel_x_1000 = round(self->zero_bias_accels[0] * 1000);
        // cdata.field.bias_accel_y_1000 = round(self->zero_bias_accels[1] * 1000);
        // cdata.field.bias_accel_z_1000 = round(self->zero_bias_accels[2] * 1000);
        // cdata.field.pitch_1000 = self->zero_bias_pitch * 1000;
        // cdata.field.roll_1000 = self->zero_bias_roll * 1000;
        
        cdata.field.bias_accel_x_1000 = round(self->zero_bias_accels[0] * 1000);
        cdata.field.bias_accel_y_1000 = round(self->zero_bias_accels[1] * 1000);
        cdata.field.bias_accel_z_1000 = round(self->zero_bias_accels[2] * 1000);
        cdata.field.pitch_1000 = self->zero_bias_pitch * 1000;
        cdata.field.roll_1000 = self->zero_bias_roll * 1000;

        printf("calibrated:%ld\n", cdata.field.is_calibrated);
        printf("G:%ld\n", cdata.field.g_1000);
        printf("bias accel x:%ld\n", cdata.field.bias_accel_x_1000);
        printf("bias accel y:%ld\n", cdata.field.bias_accel_y_1000);
        printf("bias accel z:%ld\n", cdata.field.bias_accel_z_1000);
        printf("pitch:%ld\n", cdata.field.pitch_1000);
        printf("roll:%ld\n", cdata.field.roll_1000);
        sconf_save_int64(SENSOR_CALIBRATION_CONF, (const char**)calibration_field_names, (int64_t*)cdata.arr, E_FIELD_MAX);
#else
        calibration_config[E_CALIBRATED].value.int64 = self->is_calibration_completed;
        calibration_config[E_G_1000].value.number = self->G * 1000;
        calibration_config[E_BIAS_ACCEL_X_1000].value.number = self->zero_bias_accels[0] * 1000;
        calibration_config[E_BIAS_ACCEL_Y_1000].value.number = self->zero_bias_accels[1] * 1000;
        calibration_config[E_BIAS_ACCEL_Z_1000].value.number = self->zero_bias_accels[2] * 1000;
        calibration_config[E_PITCH_1000].value.number = self->zero_bias_pitch * 1000;
        calibration_config[E_ROLL_1000].value.number = self->zero_bias_roll * 1000;

        sconf_save_with_proto(SENSOR_CALIBRATION_CONF, calibration_config, ARRAY_SIZE(calibration_config));
#endif

        return;
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
        HR_LOGE("%s(%d): can not open accelerometer ...\n", __FUNCTION__, __LINE__);
        return -1;
    }

    s->sensor->configure(s->sampling_frequency);

    s->sensor->read(&accel.self);

    if (accel.x[2] < 0) {
        s->inverted = 1;
    }

    if (s->is_calibration_completed != 1) {
        s->G = calculate_stationary_veritical_acceleration(accel.x[0], accel.x[1], accel.x[2]);
        if (s->inverted) {
            s->G *= -1.0;
        }
    }

    // update ekf status
    s->ekf.x[2] = 0;
    s->ekf.x[3] = accel.x[0];
    s->ekf.x[4] = accel.x[1];
    s->ekf.x[5] = accel.x[2];

    if (s->is_calibration_completed) {
        s->ekf.x[3] = s->zero_bias_accels[0];
        s->ekf.x[4] = s->zero_bias_accels[1];
        s->ekf.x[5] = s->zero_bias_accels[2];
    }
#if 0
    s->is_calibration_completed = 0;
    s->calibration_retries_max = s->sampling_frequency * 2;
    // s->calibration_data = (double*)calloc(sizeof(double), s->calibration_retries_max);
    s->calibration_data = (struct axis_mean*)calloc(sizeof(*s->calibration_data), s->calibration_retries_max);
#endif

    return 0;
}
static int accelerometer_stream_read(struct motion_stream* self, void* data, size_t count) {
    double dt = 0.01;
    int ret = -1;

    double accel_filtered[IMU_AXES] = {0};

    struct accelerometer_stream_data* p = (struct accelerometer_stream_data*)data;
    // double accel_union = 0, accel_filter = 0;
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

    // butter worth filter cutoff 10hz
    for (size_t i = 0; i < ARRAY_SIZE(accel_filtered); i++) {
        accel_filtered[i] = butterworth_filter_process(s->filter[i], accel.x[i] /*- s->zero_bias_accels[i]*/);
        /*double v = accel.x[i] - accel_filtered[i];
        if (fabs(v) > ACCEL_JITTER_THRESHOLD) {
            HR_LOGE("%ld -> jitter:%f ....\n", i, v);
        }*/
    }

    // it indicates that the camera is inverted, when z < 0
    // accel_union = calculate_veritical_acceleration(accel.x[0], accel.x[1], accel.x[2]);
    // accel_union = calculate_stationary_veritical_acceleration(accel_filtered[0], accel_filtered[1], accel_filtered[2]);
    // if (s->inverted) {
    //     accel_union *= -1.0;
    // }

    HR_LOGD("filtered accel:%f %f %f\n", accel_filtered[0], accel_filtered[1], accel_filtered[2]);
    _ekf_run_model(s, accel.x /*accel_filtered*/, dt);

    s->distance = s->ekf.x[0];
    s->velocity = s->ekf.x[1];

    double ca[IMU_AXES] = {
        s->ekf.x[3],
        s->ekf.x[4],
        s->ekf.x[5]};

    do_calibration_when_needed(s, ca);

    // capture data after calibration, otherwise G is not correct
    p->accel = s->ekf.x[2];
    // p->accel = calculate_veritical_acceleration(accel.x[0] - s->zero_bias_accels[0],
    //                                                     accel.x[1] - s->zero_bias_accels[1],
    //                                                     accel.x[2] - s->zero_bias_accels[2], s->zero_bias_pitch, s->zero_bias_roll);
    p->velocity = s->ekf.x[1];
    p->distance = s->ekf.x[0];

    if (s->is_calibration_completed) {
        // method one: using accel - bias
        // method two: using accel - filtered value
        // now we use first
#if 1
        double fft_accels[IMU_AXES] = {
            accel_filtered[0] - s->zero_bias_accels[0] /*- s->ekf.x[3]*/,
            accel_filtered[1] - s->zero_bias_accels[1] /*- s->ekf.x[4]*/,
            accel_filtered[2] - s->zero_bias_accels[2] /*- s->ekf.x[5]*/,
        };
#else
        double fft_accels[IMU_AXES] = {
            accel_filtered[0] - s->ekf.x[3],
            accel_filtered[1] - s->ekf.x[4],
            accel_filtered[2] - s->ekf.x[5],
        };
#endif
        _fft_process(s, fft_accels, IMU_AXES);
    }

    // mxp, 20250609, houqi's elevator jitter is very noticeable.
    // so we only care the last z axis
    p->jitter_accel = s->fft[IMU_AXES - 1].jitter_accel;
    p->jitter_frequency = s->fft[IMU_AXES - 1].jitter_frequency;

#if 0
    for (size_t i = IMU_AXES - 2; i >= 0; i++) {
        if (s->fft[i].jitter_accel > p->jitter_accel) {
            p->jitter_accel = s->fft[i].jitter_accel;
            p->jitter_frequency = s->fft[i].jitter_frequency;
        }
    }
#endif

    p->jitter_accel = round(p->jitter_accel * 100) / 100;

    if (s->is_calibration_completed != 1) {
        return -2;  // we are calibration
    }
#if 0
    if (fabs(s->ekf.x[3] - accel_filtered[0]) > ACCEL_JITTER_THRESHOLD) {
        HR_LOGE("x -> jitter:%f vs %f = (%f)....\n", s->ekf.x[3], accel_filtered[0], s->ekf.x[3] - accel_filtered[0]);
    }
    if (fabs(s->ekf.x[4] - accel_filtered[1]) > ACCEL_JITTER_THRESHOLD) {
        HR_LOGE("y -> jitter:%f vs %f = (%f)....\n", s->ekf.x[4], accel_filtered[1], s->ekf.x[4] - accel_filtered[1]);
    }
    if (fabs(s->ekf.x[5] - accel_filtered[2]) > ACCEL_JITTER_THRESHOLD) {
        HR_LOGE("z -> jitter:%f vs %f = (%f)....\n", s->ekf.x[5], accel_filtered[2], s->ekf.x[5] - accel_filtered[2]);
    }
#endif
    return 0;
}

static int accelerometer_stream_calibration_enter(struct motion_stream* self) {
    struct accelerometer_stream* s = container_of(self, struct accelerometer_stream, self);
    if (!self || !s) {
        return -1;
    }

    s->is_calibration_completed = 0;
    s->calibration_retries = 0;
    return 0;
}
static int accelerometer_stream_calibration_completed(struct motion_stream* self) {
    struct accelerometer_stream* s = container_of(self, struct accelerometer_stream, self);
    if (!self || !s) {
        return -1;
    }

    return s->is_calibration_completed;
}

static int accelerometer_stream_read_calibration_data(struct motion_stream* self, void* data, size_t count) {
    struct accelerometer_stream_calibration_data* p = (struct accelerometer_stream_calibration_data*)data;
    struct accelerometer_stream* s = container_of(self, struct accelerometer_stream, self);
    if (!self || !s || !p) {
        return -1;
    }

    assert(sizeof(struct accelerometer_stream_calibration_data) == count);

    if (s->is_calibration_completed == 0) {
        return -1;
    }

    p->bias_accel_x = s->zero_bias_accels[0];
    p->bias_accel_y = s->zero_bias_accels[1];
    p->bias_accel_z = s->zero_bias_accels[2];

    p->pitch = s->zero_bias_pitch;
    p->roll = s->zero_bias_roll;
    return 0;
}

static int accelerometer_stream_reset(struct motion_stream* self) {
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
    ms->ekf.x[2] = 0;
    ekf->P[EKF_N * 2 + 1] = 1e-10;

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
    s->self.read_calibration_data = accelerometer_stream_read_calibration_data;
    s->self.read = accelerometer_stream_read;
    s->self.reset = accelerometer_stream_reset;
    s->self.close = accelerometer_stream_close;

    for (size_t i = 0; i < ARRAY_SIZE(s->filter); i++) {
        s->filter[i] = butterworth_filter_init(BUTTERWORTH_CUTOFF_FREQUENCY, sampling_frequency);
        if (!s->filter[i]) {
        }
    }

    for (size_t i = 0; i < ARRAY_SIZE(s->calibration_mw); i++) {
        // 0.5s
        s->calibration_mw[i] = moving_window_init(sampling_frequency / 2);
    }

    for (size_t i = 0; i < ARRAY_SIZE(s->fft); i++) {
        s->fft[i].count = 0;
        s->fft[i].sum = 0;
        s->fft[i].sampling_size = 256 /*sampling_frequency*/;
        s->fft[i].in = (double*)fftw_malloc(sizeof(double) * s->fft[i].sampling_size);
        s->fft[i].out = (fftw_complex*)fftw_malloc(sizeof(fftw_complex) * (s->fft[i].sampling_size / 2 + 1));

        s->fft[i].plan = fftw_plan_dft_r2c_1d(s->fft[i].sampling_size, s->fft[i].in, s->fft[i].out, FFTW_ESTIMATE);
    }

    s->is_calibration_completed = 0;
    s->calibration_retries_max = s->sampling_frequency /** 2*/;
    // s->calibration_data = (double*)calloc(sizeof(double), s->calibration_retries_max);
    s->calibration_data = (struct axis_mean*)calloc(sizeof(*s->calibration_data), s->calibration_retries_max);

    // config load
    // CALIBRATED=1
    // G_1000=9812
    // BIAS_ACCEL_X_1000=0.0
    // BIAS_ACCEL_Y_1000=0.0
    // BIAS_ACCEL_Z_1000=0.0
    // PITCH_1000=0.12
    // ROLL_1000=0.02
#if 0
    union calibration_data cdata;

    if (0 == sconf_load_int64(SENSOR_CALIBRATION_CONF, (const char**)calibration_field_names, (int64_t*)cdata.arr, E_FIELD_MAX)) {
        printf("calibrated:%ld\n", cdata.field.is_calibrated);
        printf("g:%ld\n", cdata.field.g_1000);
        printf("bias accel x:%ld\n", cdata.field.bias_accel_x_1000);
        printf("bias accel y:%ld\n", cdata.field.bias_accel_y_1000);
        printf("bias accel z:%ld\n", cdata.field.bias_accel_z_1000);
        printf("pitch:%ld\n", cdata.field.pitch_1000);
        printf("roll:%ld\n", cdata.field.roll_1000);

        s->is_calibration_completed = cdata.field.is_calibrated;
        s->G = cdata.field.g_1000 / 1000.0;
        s->zero_bias_accels[0] = cdata.field.bias_accel_x_1000 / 1000.0;
        s->zero_bias_accels[1] = cdata.field.bias_accel_y_1000 / 1000.0;
        s->zero_bias_accels[2] = cdata.field.bias_accel_z_1000 / 1000.0;
        s->zero_bias_pitch = cdata.field.pitch_1000 / 1000.0;
        s->zero_bias_roll = cdata.field.roll_1000 / 1000.0;

        // force update filter to match zero bias
        for (int i = 0; i < 10; i++) {
            butterworth_filter_process(s->filter[0], s->zero_bias_accels[0]);
            butterworth_filter_process(s->filter[1], s->zero_bias_accels[1]);
            butterworth_filter_process(s->filter[2], s->zero_bias_accels[2]);
        }
    }

#else

    if (0 == sconf_load_with_proto(SENSOR_CALIBRATION_CONF, calibration_config, ARRAY_SIZE(calibration_config))) {
        s->is_calibration_completed = calibration_config[E_CALIBRATED].value.int64;
        s->G = calibration_config[E_CALIBRATED].value.number / 1000;
        s->zero_bias_accels[0] = calibration_config[E_BIAS_ACCEL_X_1000].value.number / 1000.0;
        s->zero_bias_accels[1] = calibration_config[E_BIAS_ACCEL_Y_1000].value.number / 1000.0;
        s->zero_bias_accels[2] = calibration_config[E_BIAS_ACCEL_Z_1000].value.number / 1000.0;
        s->zero_bias_pitch = calibration_config[E_PITCH_1000].value.number / 1000.0;
        s->zero_bias_roll = calibration_config[E_ROLL_1000].value.number / 1000.0;

        // force update filter to match zero bias
        for (int i = 0; i < 10; i++) {
            butterworth_filter_process(s->filter[0], s->zero_bias_accels[0]);
            butterworth_filter_process(s->filter[1], s->zero_bias_accels[1]);
            butterworth_filter_process(s->filter[2], s->zero_bias_accels[2]);
        }
    }
#endif

    return &s->self;
}

int accelerometer_stream_deinit(struct motion_stream* self) {
    struct accelerometer_stream* s = container_of(self, struct accelerometer_stream, self);
    if (!self || !s) {
        return -1;
    }

    for (size_t i = 0; i < ARRAY_SIZE(s->calibration_mw); i++) {
        moving_window_release(s->calibration_mw[i]);
        s->calibration_mw[i] = NULL;
    }

    if (s->calibration_data) {
        free(s->calibration_data);
        s->calibration_data = NULL;
    }

    for (size_t i = 0; i < ARRAY_SIZE(s->filter); i++) {
        if (s->filter[i]) {
            butterworth_filter_deinit(s->filter[i]);
            s->filter[i] = NULL;
        }
    }

    for (size_t i = 0; i < ARRAY_SIZE(s->fft); i++) {
        s->fft[i].count = 0;
        s->fft[i].sum = 0;

        fftw_destroy_plan(s->fft[i].plan);
        fftw_free(s->fft[i].in);
        fftw_free(s->fft[i].out);
    }

    free(s);

    fftw_cleanup();
    return 0;
}

static void _ekf_run_model(struct accelerometer_stream* self, double accel[IMU_AXES], double dt) {
    ekf_t* ekf = NULL;
    double linear_accel = 0;

    if (!self) {
        return;
    }

    ekf = &self->ekf;

    if (self->is_calibration_completed == 0) {
        linear_accel = 0;
    } else {
        // linear_accel = calculate_veritical_acceleration(accel[0], accel[1], accel[2], self->zero_bias_pitch, self->zero_bias_roll) - self->G;
#if 1
        linear_accel = calculate_veritical_acceleration(accel[0] - self->zero_bias_accels[0],
                                                        accel[1] - self->zero_bias_accels[1],
                                                        accel[2] - self->zero_bias_accels[2], self->zero_bias_pitch, self->zero_bias_roll);
#else

        linear_accel = calculate_veritical_acceleration(self->ekf.x[3] - self->zero_bias_accels[0],
                                                        self->ekf.x[4] - self->zero_bias_accels[1],
                                                        self->ekf.x[5] - self->zero_bias_accels[2], self->zero_bias_pitch, self->zero_bias_roll);
#endif
        // double linear = calculate_stationary_veritical_acceleration(self->ekf.x[3], self->ekf.x[4], self->ekf.x[5]);
        double linear = calculate_stationary_veritical_acceleration(accel[0], accel[1], accel[2]);
        // mxp, 20250509, no need process manully when we use pitch to calc accel
        // if (self->inverted) {
        //     linear_accel *= -1.0;
        // }
        HR_LOGD("%s(%d): linear %f vs %f = %f, linear - G:%f\n", __FUNCTION__, __LINE__, linear_accel, linear, linear_accel - linear, linear - self->G);
    }
    // clang-format off
    // F_k
    double F[EKF_N * EKF_N] = {
        1, dt, 0.5 * dt * dt, 0, 0, 0,
        0, 1, dt, 0, 0, 0,
        0, 0, 1, 0, 0, 0,
        0, 0, 0, 1, 0, 0,
        0, 0, 0, 0, 1, 0,
        0, 0, 0, 0, 0, 1,
    };

    // H_k
    const double H[EKF_M * EKF_N] = {
        0, 0, 1, 0, 0, 0,
        0, 0, 0, 1, 0, 0,
        0, 0, 0, 0, 1, 0,
        0, 0, 0, 0, 0, 1,
    };

    // predict
    double fx[EKF_N] = {
        ekf->x[0] + ekf->x[1] * dt + 0.5 * ekf->x[2] * dt * dt,
        ekf->x[1] + ekf->x[2] * dt,
        ekf->x[2],
        ekf->x[3],
        ekf->x[4],
        ekf->x[5]};
    // clang-format on

    // only date accel
    if (self->is_calibration_completed != 1) {
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

    HR_LOGD("a:%f, x:%f-%f-%f-%f-%f-%f\n", linear_accel, ekf->x[0], ekf->x[1], ekf->x[2], ekf->x[3], ekf->x[4], ekf->x[5]);

    if (self->is_calibration_completed == 0 || ((fabs(ekf->x[1]) != 0 && fabs(ekf->x[1]) < ZUPT_SPEED_THRESHOLD /*0.1*/) && fabs(linear_accel) < ZUPT_ACC_THRESHOLD /*0.09*/)) {
        HR_LOGD("ZUPT .............ekf->x[0]:%f, x[1]:%f, a:%f..\n", ekf->x[0], ekf->x[1], linear_accel);

        linear_accel = 0;

        fx[1] = 0;
        ekf->x[1] = 0;             // 速度置 0
        ekf->P[EKF_N + 1] = 1e-6;  // 速度误差极小，避免恢复
        fx[2] = 0;
        ekf->x[2] = 0;  // reset delta accel
        ekf->P[2 * EKF_N + 2] = 1e-10;

        // Q[ EKF_N + 1] = 1e-6;  // 降低速度噪声
        // F[1] = 0;
        // F[EKF_N + 1] = 0;
        //}
    }

    if (fabs(linear_accel) < ZUPT_ACC_THRESHOLD) {
    }

    HR_LOGD("fx: [%f, %f, %f,%f]\n", fx[0], fx[1], fx[2], fx[3]);
    HR_LOGD("x: [%f, %f, %f,%f, %f, %f]\n", ekf->x[0], ekf->x[1], ekf->x[2], ekf->x[3], ekf->x[4], ekf->x[5]);
    HR_LOGD("input: %f, %f, %f\n", accel[0], accel[1], accel[2]);
    ekf_predict(ekf, fx, F, Q);

    // do not cut off too much, realtime status should display correct
    // 0.35 ?
    if (fabs(linear_accel) < 0.05) {
        linear_accel = 0;
    }

    const double z[EKF_M] = {linear_accel, accel[0], accel[1], accel[2]};

    // HR_LOGD("delat a:%f vs %f, vs linear:%f\n", ekf->x[2], calculate_veritical_acceleration(ekf->x[3] - self->zero_bias_accels[0], ekf->x[4] - self->zero_bias_accels[1], ekf->x[5] - self->zero_bias_accels[2], self->zero_bias_pitch, self->zero_bias_roll), linear_accel);
    const double hx[EKF_M] = {ekf->x[2], ekf->x[3], ekf->x[4], ekf->x[5]};
    // if (self->calibration) {
    //     hx[0] = calculate_veritical_acceleration(ekf->x[3] - self->zero_bias_accels[0], ekf->x[4] - self->zero_bias_accels[1], ekf->x[5] - self->zero_bias_accels[2],
    //                                              self->zero_bias_pitch, self->zero_bias_roll);
    // }
    //  HR_LOGD("z: [%f, %f]\n", z[0], z[1]);
    //  HR_LOGD("hx: [%f, %f]\n", hx[0], hx[1]);
    ekf_update(ekf, z, hx, H, R);

    HR_LOGD("after x: [%f, %f, %f,%f, %f, %f]\n", ekf->x[0], ekf->x[1], ekf->x[2], ekf->x[3], ekf->x[4], ekf->x[5]);
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
            double frequency = 0;
            double accel_value = 0;
            int max_index = -1;
            double max_magnitude = 0.0;
            // reuse in buffer
            double* magnitudes = (double*)malloc(sizeof(double) * (f->sampling_size / 2 + 1));
            int N_fft_out = f->sampling_size / 2 + 1;

            double mean = f->sum / f->sampling_size;

            double window_sum = 0.0;
            for (size_t j = 0; j < f->sampling_size; j++) {
                double window_val = hanning_window(j, f->sampling_size);
                f->in[j] = (f->in[j] - mean) * window_val;
                window_sum += window_val;
            }
            // apply_hanning_window(f);
            f->count = 0;
            f->sum = 0;

            if (f->plan) {
                fftw_execute(f->plan);
            }

            // 这里估计需要考虑不要从 0 开始，从 1 或者 2 开始
            // 低于 2 hz 的我们不认为抖动
            // TODO
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

            frequency = (double)max_index * self->sampling_frequency / f->sampling_size;
            accel_value = (2.0 * max_magnitude) / window_sum;  // f->sampling_size;

            // HR_LOGE("aix:%d: frequency:%f, accel_value:%f(max_magnitude:%f), mean:%f\n", i, frequency, accel_value, max_magnitude, mean);
            // do not report when jitter rate under 2Hz
            // only filter accel_z, not filter x,y
            if (i == 2) {
                if (frequency < 1.0) {
                    accel_value = 0;
                }
            }
            if (accel_value > 0.01) {
                f->jitter_frequency = frequency;
                f->jitter_accel = accel_value;
            } else {
                f->jitter_frequency = 0;
                f->jitter_accel = 0;
            }
            /*if (frequency == 0) {
                for (size_t j = 0; j < f->sampling_size; j++) {
                    HR_LOGD("%s(%d): %d -> %f\n", __FUNCTION__, __LINE__, j, f->in[j]);
                }
            }*/
        }
    }

    return 0;
}
