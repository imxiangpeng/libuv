#include <errno.h>
#include <float.h>
#include <limits.h>
#include <math.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "accelerometer_stream.h"
#include "barometer_stream.h"
#include "floor.h"
#include "hr_log.h"
#include "motion.h"
#include "moving_window.h"
#include "sconf.h"
#include "sensor.h"
#include "time_utils.h"

// 20200509, mxp, 添加启动初始化阶段使用气压传感器对 imu 进行重置
// 启动阶段气压传感器用于识别是否是静止状态，然后通知 imu 模块执行校准或者归零
// 因为 imu 模块自己保存了校准数据，所以在开机的时候可能不会再次进行校准，会直接使用当前采集的数据进行速度等计算，
// 如果这个时候是运动状态（设备重启或者进程重启不知道当前是什么状态）那么我们的速度以及行程数据很可能就是不准确的。
// 所以，需要利用气压来识别等待静止状态，然后让 imu 重置，在此之前 montion 不应该将数据向外部传递。

// 20250515 我们修改后的 bmp388 驱动在测试中获取气压是非常灵敏的，不需要我们之前各种延迟 workaround 方案
// 我们在考虑另一个因素，就是极端情况下，纠正加速度的偏差

#define MOTION_EVENT_CONFIRM_FROM_PRESSURE 0

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
// please define it when release version
#define AUTO_FIXED_HEIGHT_WHEN_STOPPING 0

// 我们会保存过去 5s 的平均值 （不太准确，我们 moving window 也占用了 1秒，所以我们数据延迟 1 秒）
// 然后计算过去 5s 的方差，如果小于门限，就认为静止
// #define BAROMETTER_PRESSURE_PREDICT_STATIONARY_THRESHOLD_SECONDS 5
// #define BAROMETTER_PRESSURE_PREDICT_STATIONARY_STDDEV_THRESHOLD 0.5

// 海平面标准气压 (Pa)
#define P0 101325.0

// 温度递减率 (K/m)

static const double PRESSURE_L = 0.0065;
static const double PRESSURE_R = 8.31432;
static const double PRESSURE_M = 0.0289644;

static const double G0 = 9.80665;

static struct motion_observer* _motion_observers[10] = {0};

// 事实证明采用 200Hz效果不如 100Hz， 200Hz 情况下，噪声更明显，感觉我们需要同步调整卡尔曼滤波参数
static double ACCELEROMETER_SAMPLING_RATE_HZ = 100;
static double BAROMETER_SAMPLING_RATE_HZ = 50;  // 12.5f;

static double BAROMETER_PREDICT_STATIONARY_SLOPE = 0.1;
static double BAROMETER_PREDICT_STATIONARY_STDDEV = 1.5;

// 经过测试 3/1/0.5 秒都与加速度以及实际测量值有较大偏差
// 但是这三这个中感觉 1 秒效果比 3/0.5 两个的效果好
// 使用我们新的 bmp388 驱动，这里我们延迟 1s 就好了
static double BAROMETER_WINDOW_DELAY_SECONDS = 0;  // 1;
// static double BAROMETER_STATIONARY_DETECT_THRESHOLD_MS = 2000;

// 低于该速度的时候不更新状态，保持原有状态
static double VELOCITY_ZUPT_THRESHOLD = 0.1;

static const double BAROMETER_DISCONTINUOUS_THRESHOLD = 50.0;

static pthread_t _accel_tid = 0;
static pthread_t _barometer_tid = 0;

#define MAX_LINE_LENGTH 1000

// simulate using local csv files
#if DUMP_DATA_TO_FILE
static FILE* _dump_fp = NULL;
#endif

#define ACCEL_JITTER_STD_THRESHOLD 0.03

static double barometer_distance = 0;
static double barometer_begin = 0;
static double barometer_end = 0;
static double barometer_pressure = 0;
static double barometer_temperature = 0;

// 这里，我们定义基线楼层和气压
// 注意，这个可能是会动态变化的，因为我们更倾向于使用 1 楼（之前楼层标定时指定的基层）作为基线楼层
// 在刚刚开机或者启动的时候，这个可能是任意楼层
// 在运行过程中，当我们停靠基层的时候，如果检测这里不是基层，会将这里更新为基层
// 目前我们也没有实际用途，仅仅是计算高度信息，然后打印出来
static int floor_baseline_num = INT_MAX;
static double floor_baseline_pressure = 0;
static double barometer_pressure_height_relative_base_floor = 0;

static double barometer_height_discontinuous = 0;

static int MOTION_PERIOD_UPDATE_PRESSURE_WHEN_STATIONARY = 60 * 2;  // 60s
static int64_t _motion_stationary_update_pressure_threshold_time_ns = 0;
// current stationary pressure begin point
static double _motion_stationary_pressure = 0;

enum motion_init_status {
    MOTION_INIT_STATUS_UNKNOWN = 0,
    MOTION_INIT_STATUS_BAROMETER_STATIONARY = 1 << 0,
    MOTION_INIT_STATUS_ACCELEROMETER_CALIBRATION = 1 << 2,
    MOTION_INIT_STATUS_ACCELEROMETER_CALIBRATRION_COMPLETED = 1 << 3,
    MOTION_INIT_STATUS_FINISHED = 1 << 4,
};

static uint32_t _motion_init_status = MOTION_INIT_STATUS_UNKNOWN;

enum calibration_state {
    CALIB_ST_NOT_STARTED = 0,
    CALIB_ST_IN_PROGRESS,
    CALIB_ST_FINISHED,
    CALIB_ST_FAILED
};
struct accelerometer_stream {
    struct motion_stream* stream;
    double height;    // --> physical height
    double distance;  // current running distance, maybe reset to zero when running finished
    double velocity;  // velocity, +-
    enum calibration_state calib_state;

    enum motion_state state;

    struct motion_event ev;
} _accelerometer_motion;

struct barometer_stream {
    struct motion_stream* stream;
    double height;
    double distance;
    double velocity;

    int is_calibration;

    enum motion_state state;
    double motion_pressure;
    struct moving_window* mw;
    int64_t delay_stop_ts_ns;
} _barometer_motion;

static int notify_observer(enum motion_observer_action action, void* data);

static const char* motion_state_str(enum motion_state state) {
    switch (state) {
        case STOPPED:
            return "stopped";
        case ACCELERATING:
            return "accelerating";
        case DECELERATING:
            return "decelerating";
        case CONSTANTING:
            return "constanting";
    }
    return "";
}

// The pressure has already been temperature-compensated,
// so we will no longer consider the effect of temperature here.
// 但是我通过 result 结果分析发现，其实是在考虑温度因素情况下才是准确的
// 否则我们的数据与加速度结果偏差就会比较大
static double calculate_height_difference(double p0, double p1, double temperature) {
    static double fac = PRESSURE_L * PRESSURE_R / PRESSURE_M / G0;
    return ((temperature + 273.15) / PRESSURE_L) * (1 - pow(p1 / p0, fac /*0.190284*/));
}

static void* _accelerometer_thread_routin(void* args) {
    (void)args;

    int64_t delta_time_ns = seconds_to_nanoseconds(1) / ACCELEROMETER_SAMPLING_RATE_HZ;

    int floor_num = 0;
    char floor_label[64] = {0};
    int64_t delta_predict_floor_time_ns = 0;

#if MOTION_EVENT_CONFIRM_FROM_PRESSURE
    int64_t motion_event_delay_confirm_with_pressure_ns = 0;
#endif

#if DUMP_DATA_TO_FILE
    char buf[MAX_LINE_LENGTH] = {0};
    if (_dump_fp) {
        snprintf(buf, sizeof(buf), "now,now_rt,floor,accel,velocity,distance,height,pressure,temperature,pressure_height,floor_baseline_num,floor_baseline_pressure,pressure_heigh_relative_base\n");
        fwrite(buf, 1, strlen(buf), _dump_fp);
    }
#endif

    _accelerometer_motion.calib_state = CALIB_ST_NOT_STARTED;
    struct motion_stream* input = _accelerometer_motion.stream;
    struct accelerometer_stream_data result;

    // mxp, 20250606, workaround, please fixed later
    // real elevator scenarios, we observed significant acceleration jitter.
    // In some cases, the calculated speed has already dropped to zero,
    // but the elevator is still in the process of stopping and exhibits noticeable acceleration.
    // As a temporary solution, we implemented a forced reset if a stop is detected within 2 seconds.
    int64_t stopping_time_ns = 0;

    // mxp, 20250609, add confidence for floor num
    // we only update floor model when floor num is confidenced
    int floor_num_confidence = 0;

    for (;;) {
        struct timespec spec;
        int64_t now = get_monotonic_nanoseconds();

        int ret = input->read(input, (void*)&result, sizeof(result));
        // support simulate, because simulate read data only in imu thread
        // mxp, 20250515, do calibration when startup
        if ((_motion_init_status & MOTION_INIT_STATUS_FINISHED) == 0) {
            // 1. wait barameter detect stationary
            // barometer thread will update MOTION_INIT_STATUS_BAROMETER_STATIONARY of _motion_init_status
            if ((_motion_init_status & MOTION_INIT_STATUS_BAROMETER_STATIONARY) == 0) {
                HR_LOGD("%s(%d): 1. wait barometer stationary signal! motion init status: 0x%X\n", __FUNCTION__, __LINE__, _motion_init_status);

                // 1.1 clear calibration bit when barometer is not stationary
                _motion_init_status &= ~MOTION_INIT_STATUS_ACCELEROMETER_CALIBRATION;
                _motion_init_status &= ~MOTION_INIT_STATUS_ACCELEROMETER_CALIBRATRION_COMPLETED;
                goto next_iteration;
            }

            // 2. do calibration detection or reset state when calibration is completed
            if ((_motion_init_status & MOTION_INIT_STATUS_ACCELEROMETER_CALIBRATION) == 0) {
                HR_LOGD("%s(%d): 2. do calibration or reset! motion init status: 0x%X\n", __FUNCTION__, __LINE__, _motion_init_status);

                _motion_init_status |= MOTION_INIT_STATUS_ACCELEROMETER_CALIBRATION;
                // 2.1 only reset status when device is calibrated
                // 2.2 do calibration when needed
                if (input->calibration_completed(input)) {
                    input->reset(input);
                } else {
                    input->enter_calibration(input);
                }
                // _accelerometer_motion.calib_state = CALIB_ST_IN_PROGRESS;

                goto next_iteration;
            }

            // 3. wait calibration finished
            if ((_motion_init_status & MOTION_INIT_STATUS_ACCELEROMETER_CALIBRATION) == 0) {
                HR_LOGD("%s(%d): 3. wait calibration finished! motion init status: 0x%X\n", __FUNCTION__, __LINE__, _motion_init_status);

                if (input->calibration_completed(input)) {
                    _motion_init_status |= MOTION_INIT_STATUS_ACCELEROMETER_CALIBRATRION_COMPLETED;
                }
                goto next_iteration;
            }

            // TODO
            // 4 we should update motion height according barometer pressure data
            {
                double height = 0;
                int num = 0;
                char label[256] = {0};
                HR_LOGD("%s(%d): 4. calibration finished, detect floor according pressure! motion init status: 0x%X\n", __FUNCTION__, __LINE__, _motion_init_status);
                HR_LOGD("boot startup finished current pressure:%f\n", barometer_pressure);
                if (floor_predict_with_pressure(barometer_pressure, &height, &num, label, sizeof(label)) == 0) {
                    _accelerometer_motion.height = height;
                    _barometer_motion.height = height;
                    floor_baseline_num = num;
                    floor_baseline_pressure = barometer_pressure;
                    HR_LOGD("boot startup finished at: floor: %d, height:%f\n", num, height);

                    floor_update_pressure_when_stationary(num, barometer_pressure, barometer_temperature, 1);
                } else {
                    HR_LOGE("we can not map to floor number ! pressure: %f, temp:%f\n", barometer_pressure, barometer_pressure);
                    _accelerometer_motion.height = 0;
                    _barometer_motion.height = 0;
                    // unknown
                    floor_baseline_num = 1;  // 1;
                    floor_baseline_pressure = barometer_pressure;
                }
            }

            _motion_init_status |= MOTION_INIT_STATUS_FINISHED;

            // read current pressure and detect floor height
        }

        if (ret != 0) {
            // error or calibration not complete
            if (ret == -2 || 1 != input->calibration_completed(input)) {
                HR_LOGD("accelerometer is under calibration\n");
                if (_accelerometer_motion.calib_state != CALIB_ST_IN_PROGRESS) {
                    _accelerometer_motion.calib_state = CALIB_ST_IN_PROGRESS;

                    struct motion_sensor_calibration_event ev;

                    memset((void*)&ev, 0, sizeof(ev));
                    ev.type = SENSOR_ACCELEROMETER;
                    ev.state = 1;  //_accelerometer_motion.calib_state;
                    notify_observer(MOTION_OBSERVER_ACTION_ON_SENSOR_CALIBRATION, &ev);
                }
            }
            goto next_iteration;
        }

        // read success it mean calibration is finished
        if (_accelerometer_motion.calib_state != CALIB_ST_FINISHED) {
            struct accelerometer_stream_calibration_data data;
            struct motion_sensor_calibration_event ev;

            _accelerometer_motion.calib_state = CALIB_ST_FINISHED;

            memset((void*)&ev, 0, sizeof(ev));
            ev.type = SENSOR_ACCELEROMETER;
            ev.state = 0;  // CALIB_ST_FINISHED;

            if (input->read_calibration_data) {
                if (0 == input->read_calibration_data(input, &data, sizeof(data))) {
                    ev.value[0] = data.bias_accel_x;
                    ev.value[1] = data.bias_accel_y;
                    ev.value[2] = data.bias_accel_z;
                    ev.value[3] = data.pitch;
                    ev.value[4] = data.roll;
                }
            }
            notify_observer(MOTION_OBSERVER_ACTION_ON_SENSOR_CALIBRATION, &ev);
        }

        double accel = round(result.accel * 100) / 100;
        double velocity = round(result.velocity * 100) / 100;
        double distance = round(result.distance * 1000) / 1000;

        enum motion_state new_state = _accelerometer_motion.state;

        // mxp, 20250606, workaround, please fixed later
        if (stopping_time_ns != 0) {
            if (now < stopping_time_ns) {
                // HR_LOGE("!!!!!!!! now stopping , force reset !!!!! (left: %ld ms)\n", (stopping_time_ns - now) / 1000000);
                input->reset(input);
                goto next_iteration;
            } else {
                stopping_time_ns = 0;
                HR_LOGE("!!!!!!!! now stopped after 2 seconds\n");
            }
        }

        // we should use real velocity to detect accelerating & decelerating
        // do not use round, which may causing state jump
        if (velocity == 0) {
            // mxp, 20250606, workaround, please fixed later
            if (new_state != STOPPED) {
                stopping_time_ns = now + seconds_to_nanoseconds(2);
                HR_LOGE("!!!!!!!! now stopping until 2 seconds\n");
            }
            new_state = STOPPED;
        } else if (fabs(velocity) < VELOCITY_ZUPT_THRESHOLD) {
            // keep old state until velocity change significantly
            // new_state = STOPPED;
        } else if (fabs(result.velocity) > fabs(_accelerometer_motion.velocity)) {
            new_state = ACCELERATING;
        } else if (fabs(result.velocity) < fabs(_accelerometer_motion.velocity)) {
            new_state = DECELERATING;
        } else {
            new_state = CONSTANTING;
        }

        // force reset when velocity too high
        // should we according pressure
        if (fabs(velocity) > 10 || fabs(distance) > 300) {
            HR_LOGE("!!!!!!!! maybe should reset !!!!!\n");
            input->reset(input);
            goto next_iteration;
        }

        // use high precision value, not round!
        _accelerometer_motion.velocity = result.velocity;  // velocity;
        _accelerometer_motion.distance = result.distance;  // distance;

        // floor_predict has gap!
        // when running it maybe failed,
        // but it does not modify floor_num & floor_label
        // you can use previous value
        if (now > delta_predict_floor_time_ns || new_state != _accelerometer_motion.state) {
            double delta_a = 0;
            // detect floor num every seconds
            delta_predict_floor_time_ns = now + 500000000;  // seconds_to_nanoseconds(1);
            floor_predict(_accelerometer_motion.height + _accelerometer_motion.distance, &floor_num, (char*)&floor_label, sizeof(floor_label), &delta_a);
            HR_LOGD("predict floor:%d, height:%f, delta:%f\n", floor_num, _accelerometer_motion.height + _accelerometer_motion.distance, delta_a);

            if (floor_baseline_pressure != 0) {
                double h = 0;
                double relative_baseline_height = calculate_height_difference(floor_baseline_pressure, barometer_pressure, barometer_temperature);

                if (floor_relative_height(floor_baseline_num, &h) == 0) {
                    HR_LOGD("floor detect baseline floor:%d(%f), relative to baseline :%f, calc real base relative height:%f\n",
                            floor_baseline_num, floor_baseline_pressure, relative_baseline_height, relative_baseline_height + h);
                    relative_baseline_height += h;
                    barometer_pressure_height_relative_base_floor = relative_baseline_height;

                    int num = floor_num;
                    double delta_p = 0;

                    if (0 == floor_predict(relative_baseline_height, &num, (char*)&floor_label, sizeof(floor_label), &delta_p)) {
                        HR_LOGD("!!! correct -- predict floor: %d -> %d, height:%f, delta_p:%f vs delta_a:%f\n", floor_num, num, relative_baseline_height, delta_p, delta_a);
                        if (floor_num == num) {
                            floor_num_confidence = 100;
                        } else {
                            floor_num_confidence = 0;
                        }
                        floor_num = num;
                    }
                }
            }
            // HR_LOGD("%s(%d):accel:%f, velocity:%f, distance:%f, height:%f\n",
            //         __FUNCTION__, __LINE__, accel, velocity, distance, _accelerometer_motion.height + _accelerometer_motion.distance);
        }

        // HR_LOGD("%s(%d):barometer barometer_pressure:%f, barometer_begin:%f, barometer_end:%f, (%f)\n", __FUNCTION__, __LINE__, barometer_pressure, barometer_begin, barometer_end, barometer_pressure - barometer_end);
        if (new_state != _accelerometer_motion.state) {
            HR_LOGD("motion state: %d -> %d %s ==> %s, pressure:%f\n", _accelerometer_motion.state, new_state, motion_state_str(_accelerometer_motion.state), motion_state_str(new_state), barometer_pressure);
            if (_accelerometer_motion.state == STOPPED) {
                HR_LOGD("starting-------------------from:%d height:%f, pressure:%f----->\n", floor_num, _accelerometer_motion.height, barometer_pressure);
                _accelerometer_motion.ev.state = new_state;
                // accroding velocity
                _accelerometer_motion.ev.direction = _accelerometer_motion.velocity > 0 ? DIRECTION_UP : DIRECTION_DOWN;
                _accelerometer_motion.ev.height = _accelerometer_motion.height;
                _accelerometer_motion.ev.distance = distance;
                _accelerometer_motion.ev.floor = floor_num;
                _accelerometer_motion.ev.timestamp_begin = get_realtime_ms();
                // when starting, pressure is assigned , so we can compare it later
                _accelerometer_motion.ev.pressure = round(_barometer_motion.mw->mean * 100) / 100;  // 使用过去一个窗口的均值作为当前开始运行时间点气压值

#if MOTION_EVENT_CONFIRM_FROM_PRESSURE
                motion_event_delay_confirm_with_pressure_ns = now + seconds_to_nanoseconds(1);
#else
                notify_observer(MOTION_OBSERVER_ACTION_ON_EVENT, &_accelerometer_motion.ev);
#endif
            }

            if (new_state == STOPPED) {
                // 推测当前楼层，然后更正高度信息
                // 重置运动模型下次运行数据
                // if (_accelerometer_motion.velocity != 0) {
                input->reset(input);
                //}
                // real height = height + distance
                _accelerometer_motion.height += _accelerometer_motion.distance;
                _accelerometer_motion.distance = 0;
                HR_LOGD("stopping-----------at %d, height:%f, pressure:%f------------->\n", floor_num, _accelerometer_motion.height, barometer_pressure);
#if AUTO_FIXED_HEIGHT_WHEN_STOPPING
                if (0 == floor_predict(_accelerometer_motion.height, &floor_num, (char*)&floor_label, sizeof(floor_label))) {
                    HR_LOGD("update height accroding stopping floor relative height\n");
                    HR_LOGD("stopping:-------------------at:%d -> %s----->\n", floor_num, floor_label);
                    double height = _accelerometer_motion.height;
                    if (0 == floor_relative_height(floor_num, &height)) {
                        HR_LOGD("update height accroding stopping floor relative height: %d: %f -> %f\n", floor_num, _accelerometer_motion.height, height);
                        _accelerometer_motion.height = height;
                    }
                }
#endif

                _accelerometer_motion.ev.state = new_state;
                // according distance
                _accelerometer_motion.ev.direction = distance > 0 ? DIRECTION_UP : DIRECTION_DOWN;
                _accelerometer_motion.ev.height = _accelerometer_motion.height;
                _accelerometer_motion.ev.distance = distance;
                _accelerometer_motion.ev.floor_begin = _accelerometer_motion.ev.floor;
                _accelerometer_motion.ev.floor = floor_num;
                _accelerometer_motion.ev.timestamp_end = get_realtime_ms();

#if MOTION_EVENT_CONFIRM_FROM_PRESSURE
                // _accelerometer_motion.ev.pressure = barometer_pressure;  // finished using current pressure
                motion_event_delay_confirm_with_pressure_ns = now + seconds_to_nanoseconds(1);
#else
                // previous event pressure, maybe first event
                double height = calculate_height_difference(_accelerometer_motion.ev.pressure, barometer_pressure, barometer_temperature);
                double diff_percent = fabs(fabs(height) - fabs(distance)) / fmax(fabs(height), fabs(distance));
                HR_LOGD("%s(%d): stopped, pressure delta:%f detect height delta :%f vs acc height delta :%f, diff percent:%f\n", __FUNCTION__, __LINE__, barometer_pressure - _accelerometer_motion.ev.pressure, height, distance, diff_percent);

                if (fabs(height) < 1 && diff_percent > 0.1) {
                    HR_LOGD("pressure detect small distance, ignore it?\n");
                    goto next_iteration;
                }

                int num = 0;
                char label[256] = {0};
                double relative_height = 0;

                // update barometer height in accel when finished
                _barometer_motion.height += height;

                if (floor_baseline_pressure != 0) {
                    relative_height = calculate_height_difference(floor_baseline_pressure, barometer_pressure, barometer_temperature);
                    double h = 0;
                    if (floor_relative_height(floor_baseline_num, &h) == 0) {
                        HR_LOGD("mxp finished, baseline floor:%d(%f), relative to baseline :%f, calc real base relative height:%f\n", floor_baseline_num, floor_baseline_pressure, relative_height, relative_height + h);
                        relative_height += h;
                    }
                    barometer_pressure_height_relative_base_floor = relative_height;
                }

                HR_LOGD("mxp finished current pressure:%f(temp:%f), height: %f vs %f pressure base:%d floor (%f)\n", barometer_pressure, barometer_temperature, relative_height, _accelerometer_motion.height, floor_baseline_num, floor_baseline_pressure);

                if (fabs(relative_height - _accelerometer_motion.height) > 1) {
                    double diff_percent = fabs(relative_height - _accelerometer_motion.height) / fmax(fabs(relative_height), fabs(_accelerometer_motion.height));
                    HR_LOGD("mxp@barometer@accelerometer diff too much! floor:%d b:%f - a:%f = %f, diff:%f\n", floor_num, relative_height, _accelerometer_motion.height, relative_height - _accelerometer_motion.height, diff_percent);
                }

#if 1  // 1 ? 0
                // mxp, 20250609, do not trust floor_predict_with_pressure, we will update later
                if (floor_predict_with_pressure(barometer_pressure, &height, &num, label, sizeof(label)) == 0) {
                    HR_LOGD("mxp finished at : floor: %d, height:%f, while acc floor:%d\n", num, height, floor_num);

                    // if (num == floor_base_floor()) {
                     if (floor_num != num) {
                        HR_LOGE("pressure floor:%d not match with acc floor:%d, force sync\n", num, floor_num);
                        // do not update, because our predict using pressure range maybe not correct
                    //     floor_num = num;
                     }
                    // update later
                    // HR_LOGD("update height accroding stopping floor relative height: %d: %f -> %f\n", floor_num, _accelerometer_motion.height, height);
                    //_accelerometer_motion.height = height;
                    //}
                    // update baseline pressure when we run it
                    if (floor_num == floor_baseline_num) {
                        floor_baseline_pressure = barometer_pressure;
                    }

                    // adjust base floor to base floor in model
                    if (floor_num == floor_base_floor()) {
                        HR_LOGD("adjust baseline floor from %d to %d: %f(%f)\n", floor_baseline_num, floor_num, barometer_pressure, barometer_pressure - floor_baseline_pressure);
                        floor_baseline_num = floor_num;
                        floor_baseline_pressure = barometer_pressure;

                        // reset accel height, when stopped at base floor
                        // or always update accel height when stopped?
                        if (0 == floor_relative_height(floor_num, &height)) {
                            HR_LOGD("update height accroding baseline floor relative height: %d: %f -> %f\n", floor_num, _accelerometer_motion.height, height);
                            _accelerometer_motion.height = height;
                        }
                        // update in memory
                        floor_update_pressure_when_stationary(floor_num, barometer_pressure, barometer_temperature, 0);
                    }
                } else {
                    HR_LOGD("mxp finished at unknown, but from acce: floor: %d, height:%f\n", floor_num, _accelerometer_motion.height);
                }
#endif
                // update height & floor
                _accelerometer_motion.ev.height = _accelerometer_motion.height;
                _accelerometer_motion.ev.floor = floor_num;
                _accelerometer_motion.ev.pressure = barometer_pressure;  // finished using current pressure
                _accelerometer_motion.ev.temperature = barometer_temperature;
                // 当我们在电梯中跳的时候，会触发加速度的动作，由于我们的门限不一定准确，
                // 所以可能出现刚开始判定运动又停止的情况，导致上报一次距离为 0 的行程
                // 我们是在这里处理还是在 hqliftd 里面过滤呢？感觉 hqliftd 里面更好一些
                // if (fabs(distance) > 1) {
                notify_observer(MOTION_OBSERVER_ACTION_ON_EVENT, &_accelerometer_motion.ev);
                //}
#endif
            }
            _accelerometer_motion.state = new_state;
        }

#if MOTION_EVENT_CONFIRM_FROM_PRESSURE
        if (motion_event_delay_confirm_with_pressure_ns != 0 && now > motion_event_delay_confirm_with_pressure_ns) {
            motion_event_delay_confirm_with_pressure_ns = 0;
            // ev.pressure is set when running
            switch (_accelerometer_motion.ev.state) {
                case STOPPED:
                    if (_accelerometer_motion.ev.pressure == 0) {
                        break;
                    }
                    HR_LOGD("%s(%d): motion event delay confirm! STOPPED, pressure:%f vs %f = (%f)\n", __FUNCTION__, __LINE__, _accelerometer_motion.ev.pressure, barometer_pressure, barometer_pressure - _accelerometer_motion.ev.pressure);
                    // if (fabs(barometer_pressure - _accelerometer_motion.ev.pressure) < 30) {  // moving less than 2.4
                    //     // ignore the event maybe invalid!
                    //     break;
                    // }
                    double height = calculate_height_difference(_accelerometer_motion.ev.pressure, barometer_pressure, barometer_temperature);

                    HR_LOGD("%s(%d): stopped, pressure detect height:%f vs acc :%f\n", __FUNCTION__, __LINE__, height, distance);
                    if (height > 20) {
                        // maybe we should use pressure to update accelerometer
                    }
                    _accelerometer_motion.ev.pressure = barometer_pressure;
                    notify_observer(MOTION_OBSERVER_ACTION_ON_EVENT, &_accelerometer_motion.ev);

                    // double height = 0;
                    int num = 0;
                    char label[256] = {0};
                    double relative_height = 0;

                    if (floor_baseline_pressure != 0) {
                        relative_height = calculate_height_difference(floor_baseline_pressure, barometer_pressure, barometer_temperature);
                        double h = 0;
                        if (floor_relative_height(floor_baseline_num, &h) == 0) {
                            HR_LOGD("finished, baseline floor:%d, relative to baseline :%f, calc real base relative height:%f\n", floor_baseline_num, relative_height, relative_height + h);
                            relative_height += h;
                        }
                    }

                    // HR_LOGD("finished current pressure:%f, height: %f related to :%d floor\n", barometer_pressure, relative_height, floor_baseline_num);
                    if (floor_predict_with_pressure(barometer_pressure, &height, &num, label, sizeof(label)) == 0) {
                        // _accelerometer_motion.height = height;
                        // HR_LOGD("finished at : floor: %d, height:%f, while acc floor:%d\n", num, height, floor_num);
                    }
                    break;
                case ACCELERATING:
                case DECELERATING:
                case CONSTANTING:
                    HR_LOGD("%s(%d): motion event delay confirm! RUNNING, pressure:%f vs %f\n", __FUNCTION__, __LINE__, _accelerometer_motion.ev.pressure, barometer_pressure);
                    _accelerometer_motion.ev.pressure = _barometer_motion.mw->mean;  // 使用过去一个窗口的均值作为当前开始运行时间点气压值
                    notify_observer(MOTION_OBSERVER_ACTION_ON_EVENT, &_accelerometer_motion.ev);
                    break;
                default:
                    break;
            }
        }
#endif

#if 1 // mxp, 20250609, 不在这里更新，因为开关门对气压可能造成影响, 会导致 baseline 的变化
        if (_accelerometer_motion.state == STOPPED) {
            // update baseline pressure when we run it
            if (floor_num == floor_baseline_num) {
                floor_baseline_pressure = barometer_pressure;
            }
        }
#endif

        // 当长时间静止的时候，我们可能需要更新气压模型数据
        // 我们采用了两个门限，一个是来源于气压线程计算的斜率数据，一个是气压绝对值门限
        // 斜率数据，我们采用的窗口不够大，不适合单独长时间判断
        if (_accelerometer_motion.state == STOPPED &&
            ((_motion_init_status & MOTION_INIT_STATUS_BAROMETER_STATIONARY) ||
             (_motion_stationary_pressure != 0 && fabs(_motion_stationary_pressure - barometer_pressure) < 10))) {
            if (_motion_stationary_update_pressure_threshold_time_ns == 0) {
                _motion_stationary_update_pressure_threshold_time_ns = now + seconds_to_nanoseconds(MOTION_PERIOD_UPDATE_PRESSURE_WHEN_STATIONARY);
                _motion_stationary_pressure = barometer_pressure;
            }
            if (_motion_stationary_update_pressure_threshold_time_ns != 0 && now > _motion_stationary_update_pressure_threshold_time_ns) {
                HR_LOGD("%s(%d): perform pressure update after stationary duration exceeds threshold, floor:%d, pressure:%f, temp:%f, floor_num_confidence:%d\n", __FUNCTION__, __LINE__, floor_num, barometer_pressure, barometer_temperature, floor_num_confidence);
                _motion_stationary_update_pressure_threshold_time_ns = 0;
                // update only when confidence is 100
                if (floor_num_confidence == 100) {
                    floor_update_pressure_when_stationary(floor_num, barometer_pressure, barometer_temperature, 1);

                    // must make sure current floor num is correct!
                    HR_LOGD("%s(%d): also update basefloor floor:%d -> %d, pressure:%f -> %f, temp:%f\n", __FUNCTION__, __LINE__, floor_baseline_num, floor_num, floor_baseline_pressure, barometer_pressure, barometer_temperature);
                    // also update baseline floor to current floor
                    floor_baseline_pressure = barometer_pressure;
                    floor_baseline_num = floor_num;
                }
            }
        } else {
            _motion_stationary_update_pressure_threshold_time_ns = 0;
            _motion_stationary_pressure = 0;
        }

#if DUMP_DATA_TO_FILE
        if (_dump_fp) {
            snprintf(buf, sizeof(buf), "%lf,%ld,%d, %f,%f,%f,%f,%f,%f,%f,%d,%f,%f\n", (double)now / 1000000000.0, get_realtime_ms(), floor_num, accel, _accelerometer_motion.velocity, distance, _accelerometer_motion.height + _accelerometer_motion.distance, barometer_pressure, barometer_temperature, _barometer_motion.height, floor_baseline_num, floor_baseline_pressure, barometer_pressure_height_relative_base_floor);
            fwrite(buf, 1, strlen(buf), _dump_fp);
        }
#endif

        struct motion_status stat = {
            .accel = accel,
            .velocity = velocity,
            .distance = distance,
            .height = _accelerometer_motion.height + _accelerometer_motion.distance,
            .jitter_accel = result.jitter_accel,
            .jitter_frequency = result.jitter_frequency,
            .floor = atoi(floor_label),
            .running = (new_state != STOPPED),
            .pressure = barometer_pressure,
            .temperature = barometer_temperature,
            .barometer_distance = barometer_pressure_height_relative_base_floor /*barometer_distance*/,
        };

        notify_observer(MOTION_OBSERVER_ACTION_ON_STATUS, &stat);

    next_iteration:
        // HR_LOGD("now:%ld, a:%f, stddev:%f, mean:%f\n", now, data.accel_z, stddev, w->mean);
        spec.tv_sec = (now + delta_time_ns) / 1000000000;
        spec.tv_nsec = (now + delta_time_ns) % 1000000000;
        int err;
        do {
            err = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &spec, NULL);
        } while (err < 0 && errno == EINTR);
    }

#if DUMP_DATA_TO_FILE
    if (_dump_fp) {
        fclose(_dump_fp);
        _dump_fp = NULL;
    }
#endif
    HR_LOGD("finished ...\n");
    return NULL;
}

static void* _barometer_thread_routin(void* args) {
    (void)args;

    int64_t delta_time_ns = seconds_to_nanoseconds(1) / BAROMETER_SAMPLING_RATE_HZ;

    struct motion_stream* input = _barometer_motion.stream;
    double result[2] = {0};  // {pressure, temp}

    double slope = 0;
    enum motion_state prev_state = STOPPED;

    if (!_barometer_motion.mw) {
        HR_LOGE("error: can not init moving avg window\n");
        return NULL;
    }

    _barometer_motion.state = STOPPED;

    // force delay 1s
    usleep(1000 * 1000);
    while (1) {
        struct timespec spec;
        int64_t now = get_monotonic_nanoseconds();

        int ret = input->read(input, (void*)result, ARRAY_SIZE(result));
        if (ret != 0) {
            // error or calibration not complete
            if (ret == -2) {
                HR_LOGD("barometer is under calibration\n");
            }
            goto next_iteration;
        }

        double pressure = result[0];
        double temp = result[1];

        moving_window_update(_barometer_motion.mw, pressure);

        if (isnan(_barometer_motion.mw->stddev)) {
            goto next_iteration;
        }

        moving_window_slope(_barometer_motion.mw, &slope);

        if (!isnan(slope) && slope < BAROMETER_PREDICT_STATIONARY_SLOPE && _barometer_motion.mw->stddev < BAROMETER_PREDICT_STATIONARY_STDDEV) {
            if ((_motion_init_status & MOTION_INIT_STATUS_BAROMETER_STATIONARY) == 0) {
                _motion_init_status |= MOTION_INIT_STATUS_BAROMETER_STATIONARY;
                HR_LOGD("%s(%d) barometer detect stationary mean:%f, stddev:%f, slope:%f\n", __FUNCTION__, __LINE__, _barometer_motion.mw->mean, _barometer_motion.mw->stddev, slope);
            }
        } else {
            if ((_motion_init_status & MOTION_INIT_STATUS_BAROMETER_STATIONARY) != 0) {
                _motion_init_status &= ~MOTION_INIT_STATUS_BAROMETER_STATIONARY;
                HR_LOGD("%s(%d) barometer detect non stationary mean:%f, stddev:%f, slope:%f\n", __FUNCTION__, __LINE__, _barometer_motion.mw->mean, _barometer_motion.mw->stddev, slope);
            }
        }

        // HR_LOGD("barometer mean:%f, stddev:%f, slope:%f\n", _barometer_motion.mw->mean, _barometer_motion.mw->stddev, slope);

        if (barometer_pressure == 0) {
            // barometer_pressure = _barometer_motion.mw->mean;//pressure;
            barometer_pressure = pressure;
        }

        // detect jump
        if (fabs(pressure - barometer_pressure) > BAROMETER_DISCONTINUOUS_THRESHOLD) {
            barometer_height_discontinuous += calculate_height_difference(barometer_pressure, pressure, temp);
            HR_LOGD("%s(%d): mxp jump ... now:%ld, pressure:%f, previous:%f, height:%f\n",
                    __FUNCTION__, __LINE__,
                    now, pressure, barometer_pressure, barometer_height_discontinuous);
        }

        // if (barometer_now != 0) {
        //     barometer_velocity = calculate_height_difference(barometer_pressure, _barometer_motion.mw->mean /*pressure*/, temp) * seconds_to_nanoseconds(1) / (now - barometer_now);
        // }

        // barometer_now = now;

        // use mean not current point
        barometer_pressure = round(_barometer_motion.mw->mean * 100) / 100;  // pressure;
        // barometer_pressure = round(pressure * 100) / 100;
        barometer_temperature = round(temp * 100) / 100;

        if (_accelerometer_motion.state != STOPPED && prev_state == STOPPED) {
            prev_state = _accelerometer_motion.state;
            // we should record last 2 second pressure's avg
            // HR_LOGD("mxp last mean:%f vs current %f\n", _barometer_motion.mw->mean, pressure);
            barometer_begin = pressure;
            prev_state = _accelerometer_motion.state;
            _barometer_motion.delay_stop_ts_ns = 0;

            // reset discontinuous value before running
            barometer_height_discontinuous = 0;
        }
        if (_accelerometer_motion.state == STOPPED && prev_state != STOPPED) {
            prev_state = STOPPED;

            _barometer_motion.delay_stop_ts_ns = get_monotonic_nanoseconds() + seconds_to_nanoseconds(BAROMETER_WINDOW_DELAY_SECONDS);
            HR_LOGD("delay finished ....... delay_stop_ts_ns:%ld, current pressure:%f\n", _barometer_motion.delay_stop_ts_ns, pressure);
        }

        // should verify state is stopped
        if (_barometer_motion.delay_stop_ts_ns != 0) {
            if (now > _barometer_motion.delay_stop_ts_ns) {
                _barometer_motion.delay_stop_ts_ns = 0;
                barometer_end = pressure;
                // barometer_end = _barometer_motion.mw->mean;

                // barometer_distance += calculate_height_difference(barometer_begin, barometer_end, temp);

                HR_LOGD("%s(%d): mxp finished ... now:%ld, pressure begin:%f, end:%f, height:%f, discontinus:%f\n",
                        __FUNCTION__, __LINE__,
                        now, barometer_begin, barometer_end, barometer_distance,
                        barometer_height_discontinuous);
                barometer_height_discontinuous = 0;
            }
        }

    next_iteration:
        spec.tv_sec = (now + delta_time_ns) / 1000000000;
        spec.tv_nsec = (now + delta_time_ns) % 1000000000;
        int err;
        do {
            err = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &spec, NULL);
        } while (err < 0 && errno == EINTR);
    }
    return NULL;
}
#if DUMP_DATA_TO_FILE
int dump_data_init() {
    char path[256] = "elevatord-result-";
    char* ptr = path + strlen(path);
    struct tm tm;
    struct timespec ts;

    clock_gettime(CLOCK_REALTIME, &ts);
    (void)localtime_r(&ts.tv_sec, &tm);

    strftime(ptr, sizeof(path) - strlen(path) - 1, "%Y-%m-%d-%H-%M-%S", &tm);
    strcat(path, ".csv");
    HR_LOGD("dump path:%s\n", path);

    FILE* fp = fopen(path, "w+");
    if (!fp) {
        perror("open error:");
        fclose(fp);
        return -1;
    }

    _dump_fp = fp;

    return 0;
}
#endif

int motion_initalize(int argc, char** argv) {
    (void)argc;
    (void)argv;
    int ret = -1;

    enum {
        ACC_SAMPLING_RATE = 0,
        BARO_SAMPLING_RATE,
        PRESSURE_STATIONARY_SLOPE,
        PRESSURE_STATIONARY_STDDEV,
        PERIOD_UPDATE_PRESSURE,
    };

    struct sconf_proto elevatord_config[] = {
        [ACC_SAMPLING_RATE] = {"ACCELEROMETER_SAMPLING_RATE_HZ", PROTO_VALUE_NUMBER, {.number = ACCELEROMETER_SAMPLING_RATE_HZ}},
        [BARO_SAMPLING_RATE] = {"BAROMETER_SAMPLING_RATE_HZ", PROTO_VALUE_NUMBER, {.number = BAROMETER_SAMPLING_RATE_HZ}},
        [PRESSURE_STATIONARY_SLOPE] = {"BAROMETER_PREDICT_STATIONARY_SLOPE", PROTO_VALUE_NUMBER, {.number = BAROMETER_PREDICT_STATIONARY_SLOPE}},
        [PRESSURE_STATIONARY_STDDEV] = {"BAROMETER_PREDICT_STATIONARY_STDDEV", PROTO_VALUE_NUMBER, {.number = BAROMETER_PREDICT_STATIONARY_STDDEV}},
        [PERIOD_UPDATE_PRESSURE] = {"MOTION_PERIOD_UPDATE_PRESSURE_WHEN_STATIONARY", PROTO_VALUE_INT64, {.int64 = MOTION_PERIOD_UPDATE_PRESSURE_WHEN_STATIONARY}},
    };

    sconf_load_with_proto(ELEVATORD_CONFIG_PATH, elevatord_config, ARRAY_SIZE(elevatord_config));

    // accel only accept 100/200
    switch ((int)elevatord_config[ACC_SAMPLING_RATE].value.number) {
        case 100:
        case 200:
            ACCELEROMETER_SAMPLING_RATE_HZ = elevatord_config[ACC_SAMPLING_RATE].value.number;
            break;
        default:
            // ignore use origin
            break;
    }
    // accel only accept 50/12.5
    if (50.0 == elevatord_config[BARO_SAMPLING_RATE].value.number ||
        12.5 == elevatord_config[BARO_SAMPLING_RATE].value.number) {
        BAROMETER_SAMPLING_RATE_HZ = elevatord_config[BARO_SAMPLING_RATE].value.number;
    }

    HR_LOGD("accelerometer sampling rate:%fHz, barometer sampling rate:%fHz\n", ACCELEROMETER_SAMPLING_RATE_HZ, BAROMETER_SAMPLING_RATE_HZ);

    BAROMETER_PREDICT_STATIONARY_SLOPE = elevatord_config[PRESSURE_STATIONARY_SLOPE].value.number;
    BAROMETER_PREDICT_STATIONARY_STDDEV = elevatord_config[PRESSURE_STATIONARY_STDDEV].value.number;

    HR_LOGD("pressure stationary slope threshold:%f, pressure stationary stddev threshold:%f\n", BAROMETER_PREDICT_STATIONARY_SLOPE, BAROMETER_PREDICT_STATIONARY_STDDEV);

    MOTION_PERIOD_UPDATE_PRESSURE_WHEN_STATIONARY = elevatord_config[PERIOD_UPDATE_PRESSURE].value.int64;
    HR_LOGD("motion update floor pressure period :%d seconds when stationary\n", MOTION_PERIOD_UPDATE_PRESSURE_WHEN_STATIONARY);

#if DUMP_DATA_TO_FILE
    dump_data_init();
#endif

    memset((void*)&_accelerometer_motion, 0, sizeof(_accelerometer_motion));

    _accelerometer_motion.stream = accelerometer_stream_init(ACCELEROMETER_SAMPLING_RATE_HZ);
    // simulate data, initialize floor
    // _accelerometer_motion.height = -6.1;

    if (!_accelerometer_motion.stream) {
        HR_LOGE("can not find accelerometer ...\n");
        return -1;
    }

    ret = _accelerometer_motion.stream->open(_accelerometer_motion.stream);
    if (ret != 0) {
        // release
        return -1;
    }

    // do not auto enter calibration
    // calibration will be do in accel thread accroding pressure

    _barometer_motion.stream = barometer_stream_init(BAROMETER_SAMPLING_RATE_HZ);
    if (!_barometer_motion.stream) {
        HR_LOGE("can not find barometer ...\n");
        return -1;
    }

    ret = _barometer_motion.stream->open(_barometer_motion.stream);
    if (ret != 0) {
        // release
        return -1;
    }

    _barometer_motion.mw = moving_window_init((int)BAROMETER_SAMPLING_RATE_HZ /** BAROMETER_WINDOW_DELAY_SECONDS*/);
    if (!_barometer_motion.mw) {
        return -1;
    }

    // _barometer_motion.history_mw = moving_window_init(3 /*BAROMETER_SAMPLE_RATE_HZ*/ /** BAROMETER_WINDOW_DELAY_SECONDS*/);
    // acceleration_initialize();
    // barometer_initialize();
    return 0;
}

static int motion_acceleration_start(void) {
    int ret = 0;
    pthread_attr_t attr;
    struct sched_param param;
    int thread_policy;
    const int algorithm = SCHED_FIFO;  // SCHED_RR

    if (_accel_tid != 0) {
        return -1;
    }

    pthread_attr_init(&attr);

    // should set it on your real board
    // pthread_attr_setinheritsched(&attr, PTHREAD_EXPLICIT_SCHED);
    ret = pthread_attr_setschedpolicy(&attr, algorithm);
    if (0 != ret) {
        HR_LOGE("%s(%d): failed to pthread_attr_setschedpolicy\n", __FUNCTION__, __LINE__);
        return -1;
    }

    param.sched_priority = sched_get_priority_max(algorithm);
    ret = pthread_attr_setschedparam(&attr, &param);
    if (ret != 0) {
        perror("pri:");
        HR_LOGD("failed:%d ...\n", ret);
    }

    HR_LOGD("max level:%d\n", param.sched_priority);
    /*ret = pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    if (0 != ret) {
        HR_LOGE("%s(%d): failed to pthread_attr_setdetachstate\n", __FUNCTION__, __LINE__);
        return -1;
    }*/

    ret = pthread_create(&_accel_tid, &attr, _accelerometer_thread_routin, NULL);
    if (0 != ret) {
        HR_LOGE("%s(%d): failed to pthread_create\n", __FUNCTION__, __LINE__);
        return -1;
    }

    ret = pthread_getschedparam(_accel_tid, &thread_policy, &param);
    if (ret != 0) {
        perror("pri:");
        HR_LOGD("failed ...\n");
    }
    HR_LOGD("thread policy is %s, priority is %d\n",
            ((thread_policy == SCHED_FIFO) ? "FIFO" : (thread_policy == SCHED_RR ? "RR" : (thread_policy == SCHED_OTHER ? "OTHER" : "unknown"))), param.sched_priority);

    pthread_attr_destroy(&attr);

    return 0;
}

static int motion_barometer_start(void) {
    int ret = 0;
    pthread_attr_t attr;

    if (_barometer_tid != 0) {
        return -1;
    }

    pthread_attr_init(&attr);

    ret = pthread_create(&_barometer_tid, &attr, _barometer_thread_routin, NULL);
    if (0 != ret) {
        HR_LOGE("%s(%d): failed to pthread_create\n", __FUNCTION__, __LINE__);
        return -1;
    }
    pthread_attr_destroy(&attr);

    return 0;
}

int motion_run(void) {
    motion_barometer_start();
    motion_acceleration_start();
    // wait device still

    //  HR_LOGD("now device is ready ...\n");

    return 0;
}

int motion_deinitalize(void) {
    if (_barometer_tid != 0) {
        pthread_cancel(_barometer_tid);
        pthread_join(_barometer_tid, NULL);
        _barometer_tid = 0;
    }

    if (_accel_tid != 0) {
        pthread_cancel(_accel_tid);
        pthread_join(_accel_tid, NULL);
        _accel_tid = 0;
    }

    _accelerometer_motion.stream->close(_accelerometer_motion.stream);
    accelerometer_stream_deinit(_accelerometer_motion.stream);
    _accelerometer_motion.stream = NULL;
    _barometer_motion.stream->close(_barometer_motion.stream);
    barometer_stream_deinit(_barometer_motion.stream);
    _barometer_motion.stream = NULL;
    if (_barometer_motion.mw) {
        moving_window_release(_barometer_motion.mw);
        _barometer_motion.mw = NULL;
    }

#if DUMP_DATA_TO_FILE
    if (_dump_fp) {
        fclose(_dump_fp);
        _dump_fp = NULL;
    }
#endif
    return 0;
}
int motion_register_observer(struct motion_observer* observer) {
    size_t i = 0;

    for (i = 0; i < ARRAY_SIZE(_motion_observers); i++) {
        struct motion_observer* obs = _motion_observers[i];
        if (!obs) {
            _motion_observers[i] = observer;
            return 0;
        } else {
            if (obs == observer) {
                // already exists!
                return 0;
            }
        }
    }

    return -1;
}

// should use lock?
int motion_unregister_observer(struct motion_observer* observer) {
    size_t i = 0;

    for (i = 0; i < ARRAY_SIZE(_motion_observers); i++) {
        struct motion_observer* obs = _motion_observers[i];
        if (obs == observer) {
            _motion_observers[i] = 0;
            // already exists!
            return 0;
        }
    }

    return -1;
}

int motion_enter_sensor_calibration() {
    // not only support accelerometer
    if (_accelerometer_motion.stream) {
        _accelerometer_motion.stream->enter_calibration(_accelerometer_motion.stream);
    }

    // modify motion init status ?

    return 0;
}
int motion_calibrate_at_floor(int floor) {
    double height = 0;
    if (floor_relative_height(floor, &height) != 0) {
        HR_LOGD("%s(%d): calibrate at floor %d failed\n", __FUNCTION__, __LINE__, floor);
        return -1;
    }

    _accelerometer_motion.height = height;
    _barometer_motion.height = height;
    barometer_distance = height;

    // also reset baromerter baseline
    floor_baseline_num = floor;
    floor_baseline_pressure = barometer_pressure;
    barometer_pressure_height_relative_base_floor = height;

    HR_LOGD("%s(%d): calibrate at floor %d -> height: %f success\n", __FUNCTION__, __LINE__, floor, height);
    HR_LOGD("%s(%d): calibrate at floor %d -> height: %f success, baseline num:%d, baseline pressure:%f\n", __FUNCTION__, __LINE__, floor, height, floor_baseline_num, floor_baseline_pressure);
    return 0;
}

int motion_calibrate_at_height(double height) {
    int floor;
    char label[64] = {0};
    _accelerometer_motion.height = height;
    _barometer_motion.height = height;

    if (0 != floor_predict(height, &floor, label, sizeof(label))) {
        HR_LOGD("%s(%d): calibrate at height %f failed\n", __FUNCTION__, __LINE__, height);
        return -1;
    }
    HR_LOGD("%s(%d): calibrate at height %f -> floor: %d(%s) success\n", __FUNCTION__, __LINE__, height, floor, label);

    // also reset baromerter baseline
    floor_baseline_num = floor;
    floor_baseline_pressure = barometer_pressure;
    barometer_pressure_height_relative_base_floor = height;
    return 0;
}
static int notify_observer(enum motion_observer_action action, void* data) {
    size_t i = 0;

    for (i = 0; i < ARRAY_SIZE(_motion_observers); i++) {
        struct motion_observer* obs = _motion_observers[i];
        if (obs) {
            switch (action) {
                case MOTION_OBSERVER_ACTION_ON_STATUS:
                    if (obs->on_status) {
                        obs->on_status((struct motion_status*)data);
                    }
                    break;
                case MOTION_OBSERVER_ACTION_ON_EVENT:
                    if (obs->on_event) {
                        obs->on_event((struct motion_event*)data);
                    }
                    break;
                case MOTION_OBSERVER_ACTION_ON_SENSOR_CALIBRATION:
                    if (obs->on_sensor_calibration) {
                        obs->on_sensor_calibration((struct motion_sensor_calibration_event*)data);
                    }
                    break;
                default:
                    break;
            }
        }
    }
    return 0;
}
