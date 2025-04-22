#ifndef BAROMETER_STREAM_H
#define BAROMETER_STREAM_H
#include "motion_stream.h"

struct motion_stream* barometer_stream_init(int sampling_frequency);
int barometer_stream_deinit(struct motion_stream* self);
#endif