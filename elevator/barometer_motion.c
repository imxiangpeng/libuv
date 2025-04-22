#include <assert.h>
#include <math.h>
#include <stddef.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "hr_log.h"
#include "barometer_motion.h"
#include "sensor.h"
#include "time_utils.h"


#define container_of(ptr, type, member) ({            \
    const typeof(((type*)0)->member)* __mptr = (ptr); \
    (type*)((char*)__mptr - offsetof(type, member));  \
})


struct barometer_motion{
    struct motion self;

    int sampling_frequency;
    struct sensor* pressure_device;
    // struct filter* filter;
    // struct butterworth_filter* bw_filter;

    double distance;
    double velocity;
    double accel;
    double high;

    // double G;
    // enum motion_state state;

    int calibration;
    // struct moving_window* mw;

    int64_t now;
};


static int barometer_motion_read(struct motion* stream, void* data, size_t count) {
    double dt = 0.01;
    int ret = -1;
    double* p = (double*)data;
    struct sensor_data_barometer sensor;
    struct barometer_motion* bm = container_of(stream, struct barometer_motion, self);
    printf("%s(%d): ......\n", __FUNCTION__, __LINE__);
    if (!stream || !bm) {
        return -1;
    }

    ret = bm->pressure_device->read(&sensor.self);
    if (ret != 0) {
        printf("xxxxxxxxxxxxxx\n");
        return -1;
    }

    p[0] = sensor.pressure;
    p[1] = sensor.temperature;
    return 0;
}

static int barometer_motion_calibration_enter(struct motion* stream) {
    struct barometer_motion* bm = container_of(stream, struct barometer_motion, self);
    if (!stream || !bm) {
        return -1;
    }

    bm->calibration = 0;
    return 0;
}
static int barometer_motion_calibration_completed(struct motion* stream) {
     struct barometer_motion* bm = container_of(stream, struct barometer_motion, self);
    if (!stream || !bm) {
        return -1;
    }

    return 1;
    // return bm->calibration == 1;
}

static int barometer_motion_reset(struct motion* stream) {
    HR_LOGD("%s(%d): \n", __FUNCTION__, __LINE__);
    struct barometer_motion* bm = container_of(stream, struct barometer_motion, self);

    return 0;
}
static int barometer_motion_close(struct motion* stream) {
    HR_LOGD("%s(%d): \n", __FUNCTION__, __LINE__);
    return 0;
}


struct motion* barometer_motion_init(int sampling_frequency) {
    int ret = 0;
    int i = 0;
    double accel_union = 0, accel_filter = 0;
    struct sensor_data_barometer baro;
    struct barometer_motion* bm = (struct barometer_motion*)calloc(1, sizeof(struct barometer_motion));
    if (!bm) {
        return NULL;
    }

    bm->sampling_frequency = sampling_frequency;
    bm->self.enter_calibration = barometer_motion_calibration_enter;
    bm->self.calibration_completed = barometer_motion_calibration_completed;
    bm->self.runonce = barometer_motion_read;
    bm->self.reset = barometer_motion_reset;
    bm->self.close = barometer_motion_close;

    //bm->bw_filter = butterworth_filter_init(5, sampling_frequency);

    //bm->mw = moving_window_init(sampling_frequency / 2);

    bm->pressure_device = sensor_manager_get_device(SENSOR_BAROMETER);

    if (0 != bm->pressure_device->init()) {
        free(bm);
        return NULL;
    }

    bm->pressure_device->configure(bm->sampling_frequency);

    bm->pressure_device->read(&baro.self);

    bm->calibration = 0;

    return &bm->self;
}
