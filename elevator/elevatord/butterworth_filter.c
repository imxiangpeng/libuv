
#include "butterworth_filter.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct butterworth_filter{
    int order;          // 滤波器阶数 (这里固定为 2)
    double cutoff_freq; // 截止频率 (Hz)
    double sample_rate; // 采样率 (Hz)
    double b0, b1, b2;  // 前向系数 (numerator coefficients)
    double a1, a2;  // 反馈系数 (denominator coefficients)
    double x_history[2]; // 输入历史 (x[n-1], x[n-2])
    double y_history[2]; // 输出历史 (y[n-1], y[n-2])
};

static void calculate_butterworth_coefficients(struct butterworth_filter* filter) {
    double omega_c = 2.0 * M_PI * filter->cutoff_freq;
    double T = 1.0 / filter->sample_rate;
    double tan_val = tan(omega_c * T / 2.0);
    double tan_sq = tan_val * tan_val;
    double sqrt2_tan = sqrt(2.0) * tan_val;

    double denominator = 1.0 + sqrt2_tan + tan_sq;

    filter->b0 = tan_sq / denominator;
    filter->b1 = 2.0 * tan_sq / denominator;
    filter->b2 = tan_sq / denominator;
    filter->a1 = (2.0 * (tan_sq - 1.0)) / denominator;
    filter->a2 = (1.0 - sqrt2_tan + tan_sq) / denominator;
}

struct butterworth_filter* butterworth_filter_init(double cutoff_freq, double sample_rate) {
    struct butterworth_filter* filter = (struct butterworth_filter*)malloc(sizeof(struct butterworth_filter));
    if (filter == NULL) {
        perror("Failed to allocate memory for struct butterworth_filter");
        return NULL;
    }

    memset((void*)filter, 0, sizeof(*filter));

    filter->order = 2;  // 固定为二阶
    filter->cutoff_freq = cutoff_freq;
    filter->sample_rate = sample_rate;

    calculate_butterworth_coefficients(filter);

    filter->x_history[0] = filter->x_history[1] = 0.0;
    filter->y_history[0] = filter->y_history[1] = 0.0;

    return filter;
}

double butterworth_filter_process(struct butterworth_filter* filter, double input) {
    double output;

    output = filter->b0 * input +
             filter->b1 * filter->x_history[0] +
             filter->b2 * filter->x_history[1] -
             filter->a1 * filter->y_history[0] -
             filter->a2 * filter->y_history[1];

    filter->x_history[1] = filter->x_history[0];
    filter->x_history[0] = input;
    filter->y_history[1] = filter->y_history[0];
    filter->y_history[0] = output;

    return output;
}

void butterworth_filter_deinit(struct butterworth_filter* filter) {
    if (filter != NULL) {
        free(filter);
    }
}
