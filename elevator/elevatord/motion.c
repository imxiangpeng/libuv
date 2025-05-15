#include <errno.h>
#include <float.h>
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
#include "sensor.h"
#include "time_utils.h"

// 20200509, mxp, 添加启动初始化阶段使用气压传感器对 imu 进行重置
// 启动阶段气压传感器用于识别是否是静止状态，然后通知 imu 模块执行校准或者归零
// 因为 imu 模块自己保存了校准数据，所以在开机的时候可能不会再次进行校准，会直接使用当前采集的数据进行速度等计算，
// 如果这个时候是运动状态（设备重启或者进程重启不知道当前是什么状态）那么我们的速度以及行程数据很可能就是不准确的。
// 所以，需要利用气压来识别等待静止状态，然后让 imu 重置，在此之前 montion 不应该将数据向外部传递。

// 20250515 我们修改后的 bmp388 驱动在测试中获取气压是非常灵敏的，不需要我们之前各种延迟 workaround 方案
// 我们在考虑另一个因素，就是极端情况下，纠正加速度的偏差

#define MOTION_EVENT_CONFIRM_FROM_PRESSURE 1

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
// please define it when release version
#define AUTO_FIXED_HEIGHT_WHEN_STOPPING 0

// 我们会保存过去 5s 的平均值 （不太准确，我们 moving window 也占用了 1秒，所以我们数据延迟 1 秒）
// 然后计算过去 5s 的方差，如果小于门限，就认为静止
#define BAROMETTER_PRESSURE_PREDICT_STATIONARY_THRESHOLD_SECONDS 5
#define BAROMETTER_PRESSURE_PREDICT_STATIONARY_STDDEV_THRESHOLD 0.5

// 海平面标准气压 (Pa)
#define P0 101325.0

// 温度递减率 (K/m)
#define L 0.0065

static const double PRESSURE_L = 0.0065;
static const double PRESSURE_R = 8.31432;
static const double PRESSURE_M = 0.0289644;

static const double G0 = 9.81;

static struct motion_observer* _motion_observers[10] = {0};

static int ACCELEROMETER_SAMPLE_RATE_HZ = 200;
static double BAROMETER_SAMPLE_RATE_HZ = 12.5f;

// 经过测试 3/1/0.5 秒都与加速度以及实际测量值有较大偏差
// 但是这三这个中感觉 1 秒效果比 3/0.5 两个的效果好
// 使用我们新的 bmp388 驱动，这里我们延迟 1s 就好了
static double BAROMETER_WINDOW_DELAY_SECONDS = 1;  // 1;
// static double BAROMETER_STATIONARY_DETECT_THRESHOLD_MS = 2000;

// 低于该速度的时候不更新状态，保持原有状态
static double VELOCITY_ZUPT_THRESHOLD = 0.1;

static const double BAROMETER_DISCONTINUOUS_THRESHOLD = 50.0;

static pthread_t _accel_tid = 0;
static pthread_t _barometer_tid = 0;

#define MAX_LINE_LENGTH 1000

#define DUMP_DATA_TO_FILE 1
// simulate using local csv files
#if DUMP_DATA_TO_FILE
static FILE* _dump_fp = NULL;
#endif

#define ACCEL_JITTER_STD_THRESHOLD 0.03

static double barometer_distance = 0;
static double barometer_begin = 0;
static double barometer_end = 0;
static double barometer_pressure = 0;
static double barometer_velocity = 0;
static int64_t barometer_now = 0;

static double barometer_height_discontinuous = 0;

enum motion_init_stage {
    MOTION_INIT_STAGE_0 = 0,
    MOTION_INIT_STAGE_BAROMETER_STATIONARY = 1 << 0,
    MOTION_INIT_STAGE_BAROMETER_STATIONARY_CONFIRM = 1 << 1,
    MOTION_INIT_STAGE_IMU_STATIONARY_DETECT = 1 << 2,
    MOTION_INIT_STAGE_IMU_STATIONARY_DETECT_COMPLETED = 1 << 3,
    MOTION_INIT_STAGE_FINISHED = 1 << 4,
};

static uint32_t _motion_init_stage = MOTION_INIT_STAGE_0;

enum imu_calibration_state {
    IMU_CALIB_ST_WAIT_STATIONARY_SIGNAL = 0,  // init value, wait signal from barometer
    IMU_CALIB_ST_DO_CALIBRATING,
    IMU_CALIB_ST_WAIT_CALIBRATION_COMPLETED,
    IMU_CALIB_ST_WAIT_STATIONARY_CONFIRM,
    IMU_CALIB_ST_CALIBRATING,
    IMU_CALIB_ST_CALIBRATED,
    IMU_CALIB_ST_CALIBRATED_CONFIRM,
};
struct accelerometer_stream {
    struct motion_stream* stream;
    double height;    // --> physical height
    double distance;  // current running distance, maybe reset to zero when running finished
    double velocity;  // velocity, +-
    // -1: wait signal from barometer
    // 0: have received from barometer and reset imu
    // 1: imu calibrated success or reset success
    enum imu_calibration_state calib_state;

    enum motion_state state;

    struct motion_event ev;
} _accelerometer_motion;

struct barometer_stream {
    struct motion_stream* stream;
    double height;
    double distance;
    double velocity;

    int is_calibration;

    struct moving_window* mw;
    int64_t delay_stop_ts_ns;

    int stationary_pending;
    int64_t stationary_detect_threshold_ns;
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

static double calculate_height_difference(double p0, double p1, double temperature) {
    static double fac = PRESSURE_L * PRESSURE_R / PRESSURE_M / G0;
    return ((temperature + 273.15) / L) * (1 - pow(p1 / p0, fac /*0.190284*/));
}

static void* _accelerometer_thread_routin(void* args) {
    (void)args;

    int64_t delta_time_ns = seconds_to_nanoseconds(1) / ACCELEROMETER_SAMPLE_RATE_HZ;

    int floor_num = 0;
    char floor_label[64] = {0};

#if DUMP_DATA_TO_FILE
    char buf[MAX_LINE_LENGTH] = {0};
    if (_dump_fp) {
        snprintf(buf, sizeof(buf), "now,accel,velocity,distance,height,pressure,pressure_height,pressure_velocity,pressure_mean,pressure_stddev\n");
        fwrite(buf, 1, strlen(buf), _dump_fp);
    }
#endif
#if MOTION_EVENT_CONFIRM_FROM_PRESSURE
    int64_t motion_event_delay_confirm_with_pressure_ns = seconds_to_nanoseconds(1);
#endif
    _accelerometer_motion.calib_state = IMU_CALIB_ST_WAIT_STATIONARY_SIGNAL;
    struct motion_stream* input = _accelerometer_motion.stream;
    struct accelerometer_stream_data result;
    for (;;) {
        struct timespec spec;
        int64_t now = get_monotonic_nanoseconds();

        // HR_LOGD("%s(%d): motion init stage: 0x%X\n", __FUNCTION__, __LINE__, _motion_init_stage);
        int ret = input->read(input, (void*)&result, sizeof(result));
        // support simulate, because simulate read data only in imu thread
#if 0    
        if (_accelerometer_motion.calib_state == IMU_CALIB_ST_WAIT_STATIONARY_SIGNAL) {
            // HR_LOGD("we should wait barometer stationary signal ...\n");
            goto next_iteration;
        }
#endif
#if 0
        if ((_motion_init_stage & MOTION_INIT_STAGE_FINISHED) == 0) {
            // 1. wait barameter detect stationary
            if ((_motion_init_stage & MOTION_INIT_STAGE_BAROMETER_STATIONARY) == 0) {
                HR_LOGD("%s(%d): 1. wait barometer signal motion init stage: 0x%X, wait barometer stationary signal\n", __FUNCTION__, __LINE__, _motion_init_stage);
                goto next_iteration;
            }

            HR_LOGD("%s(%d): motion init stage: 0x%X, barometer stationary finished \n", __FUNCTION__, __LINE__, _motion_init_stage);
            // 2. do calibration detection or reset state when calibration is completed
            if ((_motion_init_stage & MOTION_INIT_STAGE_IMU_STATIONARY_DETECT) == 0) {
                HR_LOGD("%s(%d): 2. receive stantionary motion init stage: 0x%X, we should do imu calibration\n", __FUNCTION__, __LINE__, _motion_init_stage);
                _motion_init_stage |= MOTION_INIT_STAGE_IMU_STATIONARY_DETECT;
                if ( 0 && input->calibration_completed(input)) {
                    HR_LOGD("%s(%d): motion init stage: 0x%X, imu reset state\n", __FUNCTION__, __LINE__, _motion_init_stage);
                    input->reset(input);
                } else {
                    HR_LOGD("%s(%d): motion init stage: 0x%X, do imu calibration\n", __FUNCTION__, __LINE__, _motion_init_stage);
                    input->enter_calibration(input);
                }

                goto next_iteration;
            }

            // HR_LOGD("%s(%d): motion init stage: 0x%X, we should wait calibration finished\n", __FUNCTION__, __LINE__, _motion_init_stage);
            // 3. wait calibration finished

            if ((_motion_init_stage & MOTION_INIT_STAGE_IMU_STATIONARY_DETECT_COMPLETED) == 0) {
                if (input->calibration_completed(input)) {
                    _motion_init_stage |= MOTION_INIT_STAGE_IMU_STATIONARY_DETECT_COMPLETED;
                    HR_LOGD("%s(%d): 3. wait calibration finished motion init stage: 0x%X, calibration finished\n", __FUNCTION__, __LINE__, _motion_init_stage);
                } else {
                    HR_LOGD("%s(%d): 3. wait calibration motion init stage: 0x%X, calibration finished\n", __FUNCTION__, __LINE__, _motion_init_stage);
                }

                goto next_iteration;
            }

            // HR_LOGD("%s(%d): motion init stage: 0x%X, we should wait barometer stationary confirm signal\n", __FUNCTION__, __LINE__, _motion_init_stage);
            // 4. wait barometer confirm signal
            if ((_motion_init_stage & MOTION_INIT_STAGE_BAROMETER_STATIONARY_CONFIRM) == 0) {
                HR_LOGD("%s(%d): 4. motion init stage: 0x%X, we should wait barometer stationary confirm signal\n", __FUNCTION__, __LINE__, _motion_init_stage);
                goto next_iteration;
            }

            // 5. finish init
            _motion_init_stage |= MOTION_INIT_STAGE_FINISHED;
        }
#endif
#if 0
        if (_accelerometer_motion.calib_state == IMU_CALIB_ST_DO_CALIBRATING) {
            if (input->calibration_completed(input)) {
                input->reset(input);
                _accelerometer_motion.calib_state = IMU_CALIB_ST_WAIT_STATIONARY_CONFIRM;
            } else {
                input->enter_calibration(input);
                _accelerometer_motion.calib_state = IMU_CALIB_ST_WAIT_CALIBRATION_COMPLETED;
            }
        }
        
        if (_accelerometer_motion.calib_state == IMU_CALIB_ST_WAIT_CALIBRATION_COMPLETED) {
            if (input->calibration_completed(input)) {
                _accelerometer_motion.calib_state = IMU_CALIB_ST_WAIT_STATIONARY_CONFIRM;
            }
        }

        if (_accelerometer_motion.calib_state != IMU_CALIB_ST_CALIBRATED) {
            goto next_iteration;
        }
#endif

        if (ret != 0) {
            // error or calibration not complete
            if (ret == -2 || 1 != input->calibration_completed(input)) {
                HR_LOGD("accelerometer is under calibration\n");
                if (_accelerometer_motion.calib_state != IMU_CALIB_ST_CALIBRATING) {
                    _accelerometer_motion.calib_state = IMU_CALIB_ST_CALIBRATING;

                    struct motion_sensor_calibration_event ev;

                    memset((void*)&ev, 0, sizeof(ev));
                    ev.type = SENSOR_ACCELEROMETER;
                    ev.is_calibrating = 1;
                    notify_observer(MOTION_OBSERVER_ACTION_ON_SENSOR_CALIBRATION, &ev);
                }
            }
            goto next_iteration;
        }

        // read success it mean calibration is finished
        if (_accelerometer_motion.calib_state != IMU_CALIB_ST_CALIBRATED) {
            struct motion_sensor_calibration_event ev;

            _accelerometer_motion.calib_state = IMU_CALIB_ST_CALIBRATED;

            memset((void*)&ev, 0, sizeof(ev));
            ev.type = SENSOR_ACCELEROMETER;
            ev.is_calibrating = 0;
            ev.value[0] = result.G;  // id 4 --> local G
            notify_observer(MOTION_OBSERVER_ACTION_ON_SENSOR_CALIBRATION, &ev);
        }

        double accel = round(result.accel * 100) / 100;
        double velocity = round(result.velocity * 100) / 100;
        double distance = round(result.distance * 1000) / 1000;

        // double velocity_old = round(result.velocity * 100) / 100;;
        enum motion_state new_state = _accelerometer_motion.state;

        // we should use real velocity to detect accelerating & decelerating
        // do not use round, which may causing state jump
        if (velocity == 0) {
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

        // use high precision value, not round!
        _accelerometer_motion.velocity = result.velocity;  // velocity;
        _accelerometer_motion.distance = result.distance;  // distance;

        floor_predict(_accelerometer_motion.height + _accelerometer_motion.distance, &floor_num, (char*)&floor_label, sizeof(floor_label));
        HR_LOGD("%s(%d):accel:%f, velocity:%f, distance:%f, height:%f\n",
                __FUNCTION__, __LINE__, accel, velocity, distance, _accelerometer_motion.height + _accelerometer_motion.distance);

        HR_LOGD("%s(%d):barometer barometer_velocity:%f, barometer_pressure:%f, barometer_begin:%f, barometer_end:%f, (%f)\n", __FUNCTION__, __LINE__, barometer_velocity, barometer_pressure, barometer_begin, barometer_end, barometer_pressure - barometer_end);
        if (new_state != _accelerometer_motion.state) {
            HR_LOGD("motion state: %d -> %d %s ==> %s\n", _accelerometer_motion.state, new_state, motion_state_str(_accelerometer_motion.state), motion_state_str(new_state));
            if (_accelerometer_motion.state == STOPPED) {
                HR_LOGD("mxp starting-------------------from:%d -> %s----->\n", floor_num, floor_label);
                _accelerometer_motion.ev.state = new_state;
                // accroding velocity
                _accelerometer_motion.ev.direction = _accelerometer_motion.velocity > 0 ? DIRECTION_UP : DIRECTION_DOWN;
                _accelerometer_motion.ev.distance = distance;
                _accelerometer_motion.ev.floor = floor_num;
                _accelerometer_motion.ev.timestamp_begin = get_realtime_ms();
                // when starting, pressure is assigned , so we can compare it later
                _accelerometer_motion.ev.pressure = _barometer_motion.mw->mean;  // 使用过去一个窗口的均值作为当前开始运行时间点气压值

#if MOTION_EVENT_CONFIRM_FROM_PRESSURE
                motion_event_delay_confirm_with_pressure_ns = now + seconds_to_nanoseconds(1);
#else
                notify_observer(MOTION_OBSERVER_ACTION_ON_EVENT, &_accelerometer_motion.ev);
#endif
            }

            if (new_state == STOPPED) {
                HR_LOGD("stopping------------------------>\n");
                // 推测当前楼层，然后更正高度信息
                // 重置运动模型下次运行数据
                // if (_accelerometer_motion.velocity != 0) {
                input->reset(input);
                //}
                // real height = height + distance
                _accelerometer_motion.height += _accelerometer_motion.distance;
                _accelerometer_motion.distance = 0;
                HR_LOGD("mxp stopping-----------:%f------------->\n", _accelerometer_motion.height);
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
                _accelerometer_motion.ev.distance = distance;
                _accelerometer_motion.ev.floor_begin = _accelerometer_motion.ev.floor;
                _accelerometer_motion.ev.floor = floor_num;
                _accelerometer_motion.ev.timestamp_end = get_realtime_ms();

#if MOTION_EVENT_CONFIRM_FROM_PRESSURE
                // _accelerometer_motion.ev.pressure = barometer_pressure;  // finished using current pressure
                motion_event_delay_confirm_with_pressure_ns = now + seconds_to_nanoseconds(1);
#else
                _accelerometer_motion.ev.pressure = barometer_pressure;  // finished using current pressure
                notify_observer(MOTION_OBSERVER_ACTION_ON_EVENT, &_accelerometer_motion.ev);
#endif
            }
            _accelerometer_motion.state = new_state;
        }

#if MOTION_EVENT_CONFIRM_FROM_PRESSURE
        if (motion_event_delay_confirm_with_pressure_ns != 0 && now > motion_event_delay_confirm_with_pressure_ns) {
            motion_event_delay_confirm_with_pressure_ns = 0;
            HR_LOGD("%s(%d): motion event delay confirm! state:%d, pressure:%f vs %f (%f)\n", __FUNCTION__, __LINE__, _accelerometer_motion.ev.state, _accelerometer_motion.ev.pressure, barometer_pressure, barometer_pressure - _accelerometer_motion.ev.pressure);
            switch (_accelerometer_motion.ev.state) {
                case STOPPED:
                    HR_LOGD("%s(%d): motion event delay confirm! STOPPED, pressure:%f vs %f\n", __FUNCTION__, __LINE__, _accelerometer_motion.ev.pressure, barometer_pressure);
                    if (barometer_pressure - _accelerometer_motion.ev.pressure < 30) {  // moving less than 2.4
                        // ignore the event maybe invalid!
                        break;
                    }
                    _accelerometer_motion.ev.pressure = barometer_pressure;  // 使用过去一个窗口的均值作为当前开始运行时间点气压值
                    notify_observer(MOTION_OBSERVER_ACTION_ON_EVENT, &_accelerometer_motion.ev);
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

#if DUMP_DATA_TO_FILE
        if (_dump_fp) {
            snprintf(buf, sizeof(buf), "%lf,%f,%f,%f,%f,%f,%f,%f,%f,%f\n", (double)now / 1000000000.0, accel, _accelerometer_motion.velocity, distance, _accelerometer_motion.height + _accelerometer_motion.distance, barometer_pressure, barometer_distance, barometer_velocity, _barometer_motion.mw->mean, _barometer_motion.mw->stddev);
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
            .barometer_distance = barometer_distance,
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

    // low pass filter
    // double alpha = 0.7;
    // double previous_pressure = 0;

    double pressure_history[5] = {0};            // last 5s
    int64_t stationary_detect_threshold_ns = 0;  // get_monotonic_nanoseconds() + seconds_to_nanoseconds(BAROMETER_WINDOW_DELAY_SECONDS);

    int64_t delta_time_ns = seconds_to_nanoseconds(1) / BAROMETER_SAMPLE_RATE_HZ;

    struct motion_stream* input = _barometer_motion.stream;
    double result[2] = {0};  // {pressure, temp}

    enum motion_state prev_state = STOPPED;

    // int predict_stopped = 0;

    if (!_barometer_motion.mw) {
        HR_LOGE("error: can not init moving avg window\n");
        return NULL;
    }

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

        // double pressure_previous = _barometer_motion.mw->mean;
        double pressure = result[0];
        // pressure = alpha * pressure + (1.0f - alpha) * previous_pressure;
        // previous_pressure = pressure;
        double temp = result[1];

        moving_window_update(_barometer_motion.mw, pressure);

        if (isnan(_barometer_motion.mw->stddev)) {
            goto next_iteration;
        }
        if (pressure_history[0] == 0) {
            pressure_history[0] = _barometer_motion.mw->mean;

            goto next_iteration;
        }

        if (stationary_detect_threshold_ns == 0) {
            stationary_detect_threshold_ns = now + seconds_to_nanoseconds(1);
        }

        if (pressure_history[0] == 0) {
            pressure_history[0] = _barometer_motion.mw->mean;
        }
#if 0
        if (now > stationary_detect_threshold_ns) {
            double sum = 0, mean = 0;
            stationary_detect_threshold_ns = now + seconds_to_nanoseconds(1);
            for (size_t i = ARRAY_SIZE(pressure_history) - 1; i > 0; i--) {
                pressure_history[i] = pressure_history[i - 1];
                sum += pressure_history[i - 1];
            }
            pressure_history[0] = _barometer_motion.mw->mean;

            sum += pressure_history[0];

            mean = sum / (int)ARRAY_SIZE(pressure_history);
            sum = 0;
            for (size_t i = 0; i < ARRAY_SIZE(pressure_history); i++) {
                sum += (pressure_history[i] - mean) * (pressure_history[i] - mean);
                HR_LOGD("%d seconds history: %f\n", i, pressure_history[i]);
            }
            double stddev = sqrt(sum / (int)ARRAY_SIZE(pressure_history));

            HR_LOGD("%d seconds history: mean:%f, stddev:%f\n", ARRAY_SIZE(pressure_history), mean, stddev);

            if (stddev < BAROMETTER_PRESSURE_PREDICT_STATIONARY_STDDEV_THRESHOLD) {
                if (predict_stopped != 0) {
                    predict_stopped = 1;
                    HR_LOGD("pressure predict it's still .............\n");
                }

                if ((_motion_init_stage & MOTION_INIT_STAGE_BAROMETER_STATIONARY) == 0) {
                    HR_LOGD("%s(%d): motion init stage: 0x%X\n", __FUNCTION__, __LINE__, _motion_init_stage);
                    _motion_init_stage |= MOTION_INIT_STAGE_BAROMETER_STATIONARY;
                    HR_LOGD("%s(%d): motion init stage: 0x%X\n", __FUNCTION__, __LINE__, _motion_init_stage);
                    HR_LOGD("pressure predict it's still motion_init_stage stationary signal.............\n");
                } else {
                    if ((_motion_init_stage & MOTION_INIT_STAGE_BAROMETER_STATIONARY_CONFIRM) == 0) {
                    HR_LOGD("%s(%d): motion init stage: 0x%X\n", __FUNCTION__, __LINE__, _motion_init_stage);
                        _motion_init_stage |= MOTION_INIT_STAGE_BAROMETER_STATIONARY_CONFIRM;
                    HR_LOGD("%s(%d): motion init stage: 0x%X\n", __FUNCTION__, __LINE__, _motion_init_stage);
                        HR_LOGD("pressure predict it's still motion_init_stage stationary confirm signal.............\n");
                    }
                }

            } else {
                if (predict_stopped == 1) {
                    predict_stopped = 0;
                    HR_LOGD("pressure predict it's not still .............\n");
                }

                if ((_motion_init_stage & MOTION_INIT_STAGE_FINISHED) == 0) {
                    HR_LOGD("pressure predict it's not still, clear motion_init_stage stationary .............\n");

                    HR_LOGD("%s(%d): motion init stage: 0x%X\n", __FUNCTION__, __LINE__, _motion_init_stage);
                    _motion_init_stage &= ~(MOTION_INIT_STAGE_BAROMETER_STATIONARY);
                    HR_LOGD("%s(%d): motion init stage: 0x%X\n", __FUNCTION__, __LINE__, _motion_init_stage);
                }
            }
        }
#endif
#if 0
        if (_accelerometer_motion.calib_state == IMU_CALIB_ST_WAIT_STATIONARY_SIGNAL) {
            if (fabs(_barometer_motion.mw->stddev) < 0.5) {
                if (_barometer_motion.stationary_pending == 0) {
                    HR_LOGD("trigger barometer mean:%f, stddev:%f\n", _barometer_motion.mw->mean, _barometer_motion.mw->stddev);
                    _barometer_motion.stationary_pending = 1;
                    _barometer_motion.stationary_detect_threshold_ns = get_monotonic_nanoseconds() + seconds_to_nanoseconds(BAROMETER_WINDOW_DELAY_SECONDS);
                } else if (_barometer_motion.stationary_pending == 1) {
                    if (now > _barometer_motion.stationary_detect_threshold_ns) {
                        _barometer_motion.stationary_pending = 2;
                        //_barometer_motion.stationary_detect_threshold_ns = 0;
                        // trigger accelerometer to reset or calibration
                        // _accelerometer_motion.calib_state = 1;
                        HR_LOGD("still, go go go barometer mean:%f, stddev:%f\n", _barometer_motion.mw->mean, _barometer_motion.mw->stddev);
                        _barometer_motion.stationary_detect_threshold_ns = get_monotonic_nanoseconds() + seconds_to_nanoseconds(BAROMETER_WINDOW_DELAY_SECONDS);
                    }
                } else if (_barometer_motion.stationary_pending == 2) {
                    if (now > _barometer_motion.stationary_detect_threshold_ns) {
                        _barometer_motion.stationary_pending = 0;
                        _barometer_motion.stationary_detect_threshold_ns = 0;
                        HR_LOGD("still, confirmed go go go barometer mean:%f, stddev:%f\n", _barometer_motion.mw->mean, _barometer_motion.mw->stddev);
                    }
                }
            } else {
                HR_LOGD("failed barometer mean:%f, stddev:%f\n", _barometer_motion.mw->mean, _barometer_motion.mw->stddev);
                if (_barometer_motion.stationary_pending == 1) {
                    _barometer_motion.stationary_pending = 0;
                    _barometer_motion.stationary_detect_threshold_ns = 0;
                } else if (_barometer_motion.stationary_pending == 2) {
                    HR_LOGD("failed not confirmed again! barometer mean:%f, stddev:%f\n", _barometer_motion.mw->mean, _barometer_motion.mw->stddev);
                    // force notify accel to wait
                    _accelerometer_motion.calib_state = IMU_CALIB_ST_WAIT_STATIONARY_SIGNAL;
                    _barometer_motion.stationary_pending = 0;
                    _barometer_motion.stationary_detect_threshold_ns = 0;
                }
            }
        }
#endif
        HR_LOGD("barometer mean:%f, stddev:%f\n", _barometer_motion.mw->mean, _barometer_motion.mw->stddev);

#if 1
        if ((_motion_init_stage & MOTION_INIT_STAGE_FINISHED) == 0) {
            if (_barometer_motion.mw->stddev < BAROMETTER_PRESSURE_PREDICT_STATIONARY_STDDEV_THRESHOLD) {
                if ((_motion_init_stage & MOTION_INIT_STAGE_BAROMETER_STATIONARY) == 0) {
                    HR_LOGD("%s(%d): motion init stage: 0x%X\n", __FUNCTION__, __LINE__, _motion_init_stage);
                    _motion_init_stage |= MOTION_INIT_STAGE_BAROMETER_STATIONARY;
                    HR_LOGD("%s(%d): motion init stage: 0x%X\n", __FUNCTION__, __LINE__, _motion_init_stage);
                    HR_LOGD("pressure predict it's still motion_init_stage stationary signal.............\n");
                    // 气压数据变化比加速度慢很多，我们强制设置 5 秒看看
                    stationary_detect_threshold_ns = now + seconds_to_nanoseconds(5);
                } else {
                    if (now > stationary_detect_threshold_ns) {
                        if ((_motion_init_stage & MOTION_INIT_STAGE_BAROMETER_STATIONARY_CONFIRM) == 0) {
                            HR_LOGD("%s(%d): motion init stage: 0x%X\n", __FUNCTION__, __LINE__, _motion_init_stage);
                            if ((_motion_init_stage & MOTION_INIT_STAGE_IMU_STATIONARY_DETECT_COMPLETED) != 0) {
                                _motion_init_stage |= MOTION_INIT_STAGE_BAROMETER_STATIONARY_CONFIRM;
                                HR_LOGD("%s(%d): motion init stage: 0x%X\n", __FUNCTION__, __LINE__, _motion_init_stage);
                                HR_LOGD("pressure predict it's still motion_init_stage stationary confirm signal.............\n");
                            } else {
                                HR_LOGD("%s(%d): pressure predict  is still, but should wait imu calibration finished motion init stage: 0x%X\n", __FUNCTION__, __LINE__, _motion_init_stage);
                            }
                        }
                    }
                }
            } else {
                // if ((_motion_init_stage & MOTION_INIT_STAGE_FINISHED) == 0) {
                HR_LOGD("pressure predict it's not still, clear motion_init_stage stationary .............\n");

                HR_LOGD("%s(%d): motion init stage: 0x%X\n", __FUNCTION__, __LINE__, _motion_init_stage);
                _motion_init_stage &= ~(MOTION_INIT_STAGE_BAROMETER_STATIONARY);
                _motion_init_stage &= ~(MOTION_INIT_STAGE_BAROMETER_STATIONARY_CONFIRM);
                _motion_init_stage = 0;
                // 或许，我们还需要删除已经校准的数据，需要吗？因为校准本身也是会识别静止的
                // 但是 imu 的校准在匀速阶段可能也可以进行
                HR_LOGD("%s(%d): motion init stage: 0x%X\n", __FUNCTION__, __LINE__, _motion_init_stage);
                //}
            }
        }
#endif

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

        if (barometer_now != 0) {
            barometer_velocity = calculate_height_difference(barometer_pressure, _barometer_motion.mw->mean /*pressure*/, temp) * seconds_to_nanoseconds(1) / (now - barometer_now);
        }

        barometer_now = now;
        // barometer_pressure = _barometer_motion.mw->mean;//pressure;
        barometer_pressure = pressure;

        if (_accelerometer_motion.state != STOPPED && prev_state == STOPPED) {
            prev_state = _accelerometer_motion.state;
            // we should record last 2 second pressure's avg
            HR_LOGD("mxp last mean:%f vs current %f\n", _barometer_motion.mw->mean, pressure);
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
                barometer_end = _barometer_motion.mw->mean;

                barometer_distance += calculate_height_difference(barometer_begin, barometer_end, temp);

                HR_LOGD("%s(%d): mxp finished ... now:%ld, pressure begin:%f, end:%f, height:%f, discontinus:%f\n",
                        __FUNCTION__, __LINE__,
                        now, barometer_begin, barometer_end, barometer_distance,
                        barometer_height_discontinuous);
                barometer_height_discontinuous = 0;
            }
        }
#if 0
        _barometer_motion.mw->mean = NAN;
        _barometer_motion.mw->stddev = NAN;
        _barometer_motion.mw->size = 0;
        _barometer_motion.mw->index = 0;
        _barometer_motion.mw->sum = 0;
#endif

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
    char path[256] = "result-";
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

#if DUMP_DATA_TO_FILE
    dump_data_init();
#endif

    memset((void*)&_accelerometer_motion, 0, sizeof(_accelerometer_motion));

    _accelerometer_motion.stream = accelerometer_stream_init(ACCELEROMETER_SAMPLE_RATE_HZ);
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

    // which floor are we current stopping at?
    // should update height ?
    // or we should force wait base floor trigger
    // _accelerometer_motion.stream->enter_calibration(_accelerometer_motion.stream);

    _barometer_motion.stream = barometer_stream_init(BAROMETER_SAMPLE_RATE_HZ);
    if (!_barometer_motion.stream) {
        HR_LOGE("can not find barometer ...\n");
        return -1;
    }

    ret = _barometer_motion.stream->open(_barometer_motion.stream);
    if (ret != 0) {
        // release
        return -1;
    }

    _barometer_motion.mw = moving_window_init((int)12 /*BAROMETER_SAMPLE_RATE_HZ*/ /** BAROMETER_WINDOW_DELAY_SECONDS*/);
    if (!_barometer_motion.mw) {
        return -1;
    }

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

    HR_LOGD("%s(%d): calibrate at floor %d -> height: %f success\n", __FUNCTION__, __LINE__, floor, height);
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
