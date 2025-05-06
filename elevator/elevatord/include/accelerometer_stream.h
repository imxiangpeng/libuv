
#ifndef ACCELEROMETOR_STREAM_H
#define ACCELEROMETOR_STREAM_H

#include "motion_stream.h"

struct accelerometer_stream_data {
    double accel;
    double velocity;
    double distance;
    double G;
    double jitter_frequency;
    double jitter_accel;
};
struct motion_stream* accelerometer_stream_init(int sampling_frequency);

int accelerometer_stream_deinit(struct motion_stream* self);
#endif
