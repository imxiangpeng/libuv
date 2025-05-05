// butterworth_filter.h
#ifndef BUTTERWORTH_FILTER_H
#define BUTTERWORTH_FILTER_H

struct butterworth_filter;

struct butterworth_filter* butterworth_filter_init(double cutoff_freq, double sample_rate);
double butterworth_filter_process(struct butterworth_filter* filter, double input);
void butterworth_filter_deinit(struct butterworth_filter* filter);

#endif // BUTTERWORTH_FILTER_H
