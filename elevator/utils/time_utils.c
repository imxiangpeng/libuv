#include <stdint.h>
#include <time.h>
#include "time_utils.h"

int64_t get_monotonic_nanoseconds(void) {
    struct timespec t;
    t.tv_sec = t.tv_nsec = 0;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000000000LL + t.tv_nsec;
}

int64_t get_realtime_ms() {
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) == -1) {
        return 0;
    }
    return (ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL);
}