#ifndef MOTION_STREAM_H
#define MOTION_STREAM_H
#include <stddef.h>
struct motion_stream {
    int (*open)(struct motion_stream*);
    int (*read)(struct motion_stream*, void*, size_t);
    
    int (*enter_calibration)(struct motion_stream*);
    int (*calibration_completed)(struct motion_stream*);
    int (*read_calibration_data)(struct motion_stream*, void*, size_t);
    // only reset velocity/distance
    int (*reset)(struct motion_stream*);
    int (*close)(struct motion_stream*);
};
#endif