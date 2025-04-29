#ifndef TIME_UTILS_H
#define TIME_UTILS_H

#include <stdint.h>

static inline int64_t seconds_to_nanoseconds(int64_t secs) {
    return secs * 1000000000;
}

int64_t get_monotonic_nanoseconds(void);
#endif
