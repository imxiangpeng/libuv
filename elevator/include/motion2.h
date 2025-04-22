#ifndef MOTION_H
#define MOTION_H
#include <stddef.h>
struct motion {
    int (*enter_calibration)(struct motion*);
    int (*calibration_completed)(struct motion*);
    int (*runonce)(struct motion*, void*, size_t);
    // only reset velocity/distance
    int (*reset)(struct motion*);
    int (*close)(struct motion*);
};
#endif