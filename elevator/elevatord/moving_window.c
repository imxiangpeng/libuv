// mxp, 20250412, a simple moving window which auto calc mean & stddev

#include "moving_window.h"

#include <float.h>
#include <math.h>
#include <stdlib.h>
struct moving_window* moving_window_init(int size) {
    // data is append at end of struct moving_avg_window
    // make sure it's align on 4 bytes
    int ss = ((sizeof(struct moving_window) + 3) / 4) * 4;
    struct moving_window* w = (struct moving_window*)calloc(1, ss + size * sizeof(double));
    if (!w) {
        return NULL;
    }
    w->capability = size;
    w->data = (double*)((char*)w + ss);

    w->stddev = NAN;
    return w;
}

int moving_window_release(struct moving_window *w) {
    if (w) {
        free(w);
    }
    return 0;
}
int moving_window_update(struct moving_window* w, double val) {
    double var_sum = 0.0;

    if (!w) {
        return -1;
    }

    // window full
    // remove old value from sum
    if (w->size == w->capability) {
        w->sum -= w->data[w->index];
    }
    w->data[w->index] = val;
    w->sum += val;
    w->index = (w->index + 1) % w->capability;  // circle buffer
    if (w->size != w->capability) {
        w->size++;
    }

    if (w->size != w->capability) {
        return -1;  // not full window
    }

    w->mean = w->sum / w->size;
    // HR_LOGD("capability:%d, index:%d, size:%d, mean:%f :\n", w->capability, w->index, w->size, w->mean);
    for (int i = 0; i < w->size; i++) {
        // HR_LOGD("%f", w->data[i]);
        // if (i != w->size - 1) {
        //     HR_LOGD(" ");
        // }
        var_sum += (w->data[i] - w->mean) * (w->data[i] - w->mean);
    }
    // HR_LOGD("\n");

    w->stddev = sqrt(var_sum / w->size);
    return 0;
}

int moving_window_trim_avg(struct moving_window* w, double* val) {
    double max = -DBL_MAX;
    double min = DBL_MAX;
    if (w->size < 3) {
        return -1;
    }

    for (int i = 0; i < w->size; ++i) {
        double val = w->data[i];
        if (val > max) {
            max = val;
        }
        if (val < min) {
            min = val;
        }
    }

    *val = (w->sum - max - min) / (w->size - 2);

    return 0;
}
