
#include "butterworth_filter.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void calculate_butterworth_coefficients(ButterworthFilter* filter) {
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

ButterworthFilter* butterworth_filter_init(double cutoff_freq, double sample_rate) {
    ButterworthFilter* filter = (ButterworthFilter*)malloc(sizeof(ButterworthFilter));
    if (filter == NULL) {
        perror("Failed to allocate memory for ButterworthFilter");
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

double butterworth_filter_process(ButterworthFilter* filter, double input) {
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

void butterworth_filter_deinit(ButterworthFilter* filter) {
    if (filter != NULL) {
        free(filter);
    }
}
