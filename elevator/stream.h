#ifndef STREAM_H
#define STREAM_H
#include <stddef.h>
struct stream {
    int (*enter_calibration)(struct stream*);
    int (*calibration_completed)(struct stream*);
    int (*runonce)(struct stream*, void*, size_t);
    // only reset velocity/distance
    int (*reset)(struct stream*);
    int (*close)(struct stream*);
};
#endif