/*
 * Copyright (C) 2025 Inspur Group Co., Ltd. Unpublished
 *
 * Inspur Group Co., Ltd.
 * Proprietary & Confidential
 *
 * This source code and the algorithms implemented therein constitute
 * confidential information and may comprise trade secrets of Inspur
 * or its associates, and any use thereof is subject to the terms and
 * conditions of the Non-Disclosure Agreement pursuant to which this
 * source code was originally received.
 */

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
