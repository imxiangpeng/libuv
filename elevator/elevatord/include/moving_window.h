#ifndef MOVING_WINDOW_H
#define MOVING_WINDOW_H

struct moving_window {
    int capability;
    double* data;
    int index;
    int size;
    double sum;
    double mean;
    double stddev;
};

struct moving_window* moving_window_init(int size);
int moving_window_release(struct moving_window *w);
int moving_window_update(struct moving_window* w, double val);

int moving_window_trim_avg(struct moving_window* w, double* val);

int moving_window_slope(struct moving_window *w, double *val);

#endif