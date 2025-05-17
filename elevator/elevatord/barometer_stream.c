#include "barometer_stream.h"

#include <assert.h>
#include <stddef.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// #include "butterworth_filter.h"
#include "hr_log.h"

#include "motion_stream.h"
#include "sensor.h"


#define container_of(ptr, type, member) ({            \
    const typeof(((type*)0)->member)* __mptr = (ptr); \
    (type*)((char*)__mptr - offsetof(type, member));  \
})

struct barometer_stream {
    struct motion_stream self;

    int sampling_frequency;
    struct sensor* sensor;

    double distance;
    double velocity;
    double accel;
    double high;

    int calibration;

    int64_t now;

    // struct butterworth_filter* filter;
};

static int barometer_stream_open(struct motion_stream* self) {
    HR_LOGD("%s(%d): \n", __FUNCTION__, __LINE__);
    struct sensor_data_barometer baro;
    struct barometer_stream* s = container_of(self, struct barometer_stream, self);

    s->sensor = sensor_instance(SENSOR_BAROMETER);

    if (0 != s->sensor->init()) {
        HR_LOGE("%s(%d): can open open barometer ...\n");
        return -1;
    }

    s->sensor->configure(s->sampling_frequency);

    s->sensor->read(&baro.self);

    s->calibration = 0;

    return 0;
}

static int barometer_stream_read(struct motion_stream* self, void* data, size_t count) {
    int ret = -1;
    double* p = (double*)data;
    struct sensor_data_barometer sensor;
    struct barometer_stream* s = container_of(self, struct barometer_stream, self);
    if (!self || !s || !p) {
        return -1;
    }

    assert(count >= 2);
    ret = s->sensor->read(&sensor.self);
    if (ret != 0) {
        return -1;
    }

    // {pressure, temperature}
    // p[0] = butterworth_filter_process(s->filter, sensor.pressure); // sensor.pressure;
    p[0] = sensor.pressure;
    p[1] = sensor.temperature;
    return 0;
}

static int barometer_stream_calibration_enter(struct motion_stream* self) {
    struct barometer_stream* s = container_of(self, struct barometer_stream, self);
    if (!self || !s) {
        return -1;
    }

    s->calibration = 0;
    return 0;
}
static int barometer_stream_calibration_completed(struct motion_stream* self) {
    struct barometer_stream* s = container_of(self, struct barometer_stream, self);
    if (!self || !s) {
        return -1;
    }

    return 1;
    // return bm->calibration == 1;
}

static int barometer_stream_read_calibration_data(struct motion_stream* self, void* data, size_t count) {
    (void)self;
    (void)data;
    (void)count;

    return 0;
}
static int barometer_stream_reset(struct motion_stream* self) {
    (void)self;
    HR_LOGD("%s(%d): \n", __FUNCTION__, __LINE__);

    return 0;
}
static int barometer_stream_close(struct motion_stream* self) {
    (void)self;
    HR_LOGD("%s(%d): \n", __FUNCTION__, __LINE__);
    return 0;
}

struct motion_stream* barometer_stream_init(int sampling_frequency) {
    struct barometer_stream* bm = (struct barometer_stream*)calloc(1, sizeof(struct barometer_stream));
    if (!bm) {
        return NULL;
    }

    bm->sampling_frequency = sampling_frequency;
    bm->self.open = barometer_stream_open;
    bm->self.read = barometer_stream_read;
    bm->self.reset = barometer_stream_reset;
    bm->self.enter_calibration = barometer_stream_calibration_enter;
    bm->self.calibration_completed = barometer_stream_calibration_completed;
    bm->self.read_calibration_data = barometer_stream_read_calibration_data;
    bm->self.close = barometer_stream_close;

    // bm->filter = butterworth_filter_init(1, sampling_frequency);

    // bm->mw = moving_window_init(sampling_frequency / 2);
    return &bm->self;
}

int barometer_stream_deinit(struct motion_stream* self) {
    
    struct barometer_stream* s = container_of(self, struct barometer_stream, self);
    if (!self || !s) {
        return -1;
    }
    free(s);
    return 0;
}