// butterworth_filter.h
#ifndef BUTTERWORTH_FILTER_H
#define BUTTERWORTH_FILTER_H
typedef struct butterworth_filter{
    int order;          // 滤波器阶数 (这里固定为 2)
    double cutoff_freq; // 截止频率 (Hz)
    double sample_rate; // 采样率 (Hz)
    double b0, b1, b2;  // 前向系数 (numerator coefficients)
    double a1, a2;  // 反馈系数 (denominator coefficients)
    double x_history[2]; // 输入历史 (x[n-1], x[n-2])
    double y_history[2]; // 输出历史 (y[n-1], y[n-2])
} ButterworthFilter;

// 初始化巴特沃斯滤波器
ButterworthFilter* butterworth_filter_init(double cutoff_freq, double sample_rate);

// 计算巴特沃斯滤波器系数 (内部函数，create_butterworth_filter 调用)
// void calculate_butterworth_coefficients(ButterworthFilter* filter);

// 应用巴特沃斯滤波器
double butterworth_filter_process(ButterworthFilter* filter, double input);

// 释放滤波器内存
void butterworth_filter_deinit(ButterworthFilter* filter);

#endif // BUTTERWORTH_FILTER_H
