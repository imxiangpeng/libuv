
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "hr_log.h"

#define MAX_LINE_LENGTH 1000
// simulate using local csv files
#define USE_LOCAL_SIMULATE_DATA 1

#if USE_LOCAL_SIMULATE_DATA
const char *SIMULATE_DATA_FILE = "simulate.csv";
static FILE *_simulate_data_fp = NULL;
struct simulate_data {
    double now;
    double accel_x;
    double accel_y;
    double accel_z;
    double gyro_x;
    double gyro_y;
    double gyro_z;
    double pressure;
    double temp;
};

#endif
#if USE_LOCAL_SIMULATE_DATA
static int simulate_data_init(void) {
    FILE *fp = fopen(SIMULATE_DATA_FILE, "r");
    if (!fp) return -1;

    char line[MAX_LINE_LENGTH] = {0};
    fgets(line, MAX_LINE_LENGTH, fp);  // skip csv file head
    _simulate_data_fp = fp;

    return 0;
}

static int simulate_data_read(struct simulate_data *data) {
    double now, dt, accel, pressure, temp, ag;
    char line[MAX_LINE_LENGTH];
    if (!data || !_simulate_data_fp)
        return -1;

    char *p = fgets(line, MAX_LINE_LENGTH, _simulate_data_fp);
    if (!p) return -1;

    if (sscanf(p, "%lf,%lf,%*f,%*f,%*f,%lf,%*f,%*f,%*f,%lf,%lf,%lf", &now, &dt, &accel, &pressure, &temp, &ag) != 6) {
        printf("CSV 解析错误:%s\n", line);
    }

    data->accel_x = 0;
    data->accel_y = 0;
    data->accel_z = accel;
    data->now = now;
    data->pressure = pressure;
    data->temp = temp;
    return 0;
}
#endif

#if 0
static void *_realtime_routin(void *args) {
    char buf[MAX_LINE_LENGTH] = {0};
    struct simulate_data data;
    int64_t delta_time_ns = 0;//seconds_to_nanoseconds(1) / ACCEL_SAMPLE_RATE_HZ;
    struct moving_avg_window *w = moving_average_window_init(ACCEL_SAMPLE_RATE_HZ);
    if (!w) {
        printf("error: can not init moving avg window\n");
        return NULL;
    }

#if DUMP_DATA_TO_FILE
    snprintf(buf, sizeof(buf), "now,accel,pressure,temp,mean,stddev\n");
    fwrite(buf, 1, strlen(buf), _dump_fp);
#endif

    for (;;) {
        struct timespec spec;
        int64_t now = system_mono_time_nanoseconds();

        if (simulate_data_read(&data) != 0) {
            break;
        }

        double stddev = 0;
        moving_window_stddev(w, data.accel_z, &stddev);
#if DUMP_DATA_TO_FILE
        if (_dump_fp) {
            snprintf(buf, sizeof(buf), "%f,%f,%f,%f,%f,%f\n", data.now, data.accel_z, data.pressure, data.temp, w->mean,stddev);

            fwrite(buf, 1, strlen(buf), _dump_fp);
        }
#endif
        HR_LOGD("now:%ld, a:%f, stddev:%f, mean:%f\n", now, data.accel_z, stddev, w->mean);
        spec.tv_sec = (now + delta_time_ns) / 1000000000;
        spec.tv_nsec = (now + delta_time_ns) % 1000000000;
        int err;
        do {
            err = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &spec, NULL);
        } while (err < 0 && errno == EINTR);
    }

#if USE_LOCAL_SIMULATE_DATA
    fclose(_simulate_data_fp);
    _simulate_data_fp = NULL;
#endif

#if DUMP_DATA_TO_FILE
    if (_dump_fp) {
        fclose(_dump_fp);
        _dump_fp = NULL;
    }
#endif
    printf("finished ...\n");
    return NULL;
}
#endif

#if DUMP_DATA_TO_FILE
int dump_data_init() {
    FILE *fp = fopen("result.csv", "w+");
    if (!fp) {
        perror("open error:");
        fclose(fp);
        return -1;
    }

    _dump_fp = fp;

    return 0;
}
#endif

int acceleration_initialize(int hz) {
#if USE_LOCAL_SIMULATE_DATA
    if (0 != simulate_data_init()) {
        return -1;
    }
#endif

#if DUMP_DATA_TO_FILE
    dump_data_init();
#endif

    return 0;
}

#if 0


// 海平面标准气压 (Pa)
#define P0 101325.0

// 温度递减率 (K/m)
#define L 0.0065

static const double PRESSURE_L = 0.0065;
static const double PRESSURE_R = 8.31432;
static const double PRESSURE_M = 0.0289644;

static const double G0 = 9.823;

// #define EKF_N 5  // 状态变量 [h, v, a, p, t]
#define EKF_N 4  // 状态变量 [h, v, a, ]
// #define EKF_M 3  // 观测变量 [ a_m (加速度), h_m (气压计算), temp]
#define EKF_M 1  // 观测变量 [ a_m (加速度), h_m (气压计算), temp]
                 //
#define _float_t double

#include "tinyekf.h"

struct dm_ekf {
  ekf_t ekf;
};

static /*const*/ double Q[EKF_N * EKF_N] = {
    1e-1, 0,    0, 0,
    0,    1e-1, 0, 0,
    0,    0,    1e-3, 0,
    0,    0,    0, 1e-3
};

static const double R[EKF_M * EKF_M] = {1e-3};

#if 0    
static double pressure_to_altitude(double pressure) {
    return (1.0 - pow(pressure / 1013.25, 0.1903)) * 44330.0;
}
#endif

double calculate_altitude(double pressure, double temperature) {
    static double fac = PRESSURE_L * PRESSURE_R / PRESSURE_M / G0;
    return ((temperature + 273.15) / L) * (1 - pow(pressure / P0, fac /*0.190284*/));
}

// 计算两次测量之间的高度变化
double calculate_height_difference(double pressure1, double pressure2, double temperature) {
    static double fac = PRESSURE_L * PRESSURE_R / PRESSURE_M / G0;
    return ((temperature + 273.15) / L) * (1 - pow(pressure2 / pressure1, fac /*0.190284*/));
}

static void dm_ekf_init(struct dm_ekf *self) {
    // 初始化 EKF，使用单位协方差矩阵
    const double pdiag[EKF_N] = {1, 1, 1, 1};
    ekf_initialize(&self->ekf, pdiag);
}

static double distance = 0;
static double speed = 0;
static void dm_ekf_run_model (struct dm_ekf *self, double dt, double measured_accel, double measured_pressure, double measured_temp) {
    ekf_t *ekf = &self->ekf;
    
//    double measured_height = pressure_to_altitude(measured_pressure/100);

    // 状态转移矩阵 F_k
    /*const*/ double F[EKF_N * EKF_N] = {
        1, dt, 0.5 * dt * dt, 0,
        0, 1,  dt, 0 /*0.5 * dt*/,
        0, 0,  1, 0/*1*/,
        0, 0,  0, 1
    };

    // 观测矩阵 H_k
    const double H[EKF_M * EKF_N] = {
        0, 0, 1, 0
    };

    // 预测状态
    /*const*/ double fx[EKF_N] = {
        ekf->x[0] + ekf->x[1] * dt + 0.5 * ekf->x[2] * dt * dt,
        ekf->x[1] + ekf->x[2] * dt /*+ 0.5 * ekf->x[3] * dt*/,
        ekf->x[2] /*+ ekf->x[3]*/,
        ekf->x[3],
    };

    printf("a:%f, x:%f-%f-%f-%f\n", measured_accel, ekf->x[0], ekf->x[1], ekf->x[2], ekf->x[3]);
    if (fabs(ekf->x[1]) < 0.1 && fabs(measured_accel) < 0.09) {
        printf("ZUPT ...............\n");
        fx[1] = 0;
        ekf->x[1] = 0;  // 速度置 0
        ekf->P[EKF_N + 1] = 1e-6;  // 速度误差极小，避免恢复
        // Q[ EKF_N + 1] = 1e-6;  // 降低速度噪声
        //F[1] = 0;
        //F[EKF_N + 1] = 0;
    }

    ekf_predict(ekf, fx, F, Q);

#if 0
    // 低通滤波
    float alpha = 0.1;
    static double accel_filtered = 0;
    accel_filtered = alpha * measured_accel + (1 - alpha) * accel_filtered;
  
    measured_accel = accel_filtered;
#endif

    // if (fabs(measured_accel) < 0.1) measured_accel = 0;
    if (fabs(measured_accel) < 0.03) measured_accel = 0;
 
    double s = speed;
    speed += measured_accel * dt;
    distance += 0.5 * (s + speed) * dt + 0.5 * measured_accel * dt * dt;
    if (measured_accel == 0) {
        if (fabs(speed) < 0.3)
            speed = 0;
    }
     
    printf("speed:%f, distance:%f\n", speed, distance);
    printf("p:%f, a:%f, t:%f\n", measured_pressure, measured_accel, measured_temp);
    // 观测值
    const double z[EKF_M] = {measured_accel};

    // 预测测量值
    const double hx[EKF_M] = {ekf->x[2] /*+ ekf->x[3]*/};

    printf("z:(%f) vs h:(%f)\n", z[0], hx[0]);

    ekf_update(ekf, z, hx, H, R);

}


// time,dt,accel_x,accel_y,accel_z,union_g,gyro_x,gyro_y,gyro_z,pressure,temp,ag
// 0.088057,0.068285,-0.21546,-0.234612,-9.820188,-9.825353,0,-0.005325,0.00426,97488.03,31.83,-0.000731482
void process_csv(const char *filename) {
    char buf[512] = {0};
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("无法打开 CSV 文件");
        return;
    }

    struct dm_ekf self;
    dm_ekf_init(&self);
    
    FILE *result_fd = fopen("result.csv", "w+");
    if (!result_fd) {
        printf("can not open result.csv\n");
        perror("open error:");
        fclose(file);
        return;
    }

    // system("echo 'accel,accel_variance,velocity,velocity_variance,distance,pressure0,pressure1,temp0,temp1,high,high*300,highraw' > result.csv");

    snprintf(buf, sizeof(buf), "accel,accel_filter,velocity,distance,pressure,temp,altitude,high,delat_accel\n");
    
    fwrite(buf, 1, strlen(buf), result_fd);

    char line[MAX_LINE_LENGTH];
    fgets(line, MAX_LINE_LENGTH, file); // 跳过 CSV 头部

    double default_hight = 0;
    int i = 0;
    while (fgets(line, MAX_LINE_LENGTH, file)) {

        double now, dt, accel, pressure, temp, ag;

#if 1        
        if (sscanf(line, "%lf,%lf,%*lf,%*lf,%*lf,%lf,%*lf,%*lf,%*lf,%lf,%lf,%lf", &now, &dt, &accel, &pressure, &temp, &ag) != 6) {
            printf("CSV 解析错误:%s\n", line);
            continue;
        }
#else
        if (sscanf(line, "%lf,%*lf,%*lf,%*lf,%lf,%*lf,%*lf,%lf", &now, &accel, &ag) != 3) {
            printf("CSV 解析错误:%s\n", line);
            continue;
        }
        //if (i++ < 400) continue;

        static double prev_time = 0;
        dt = now - prev_time;
        prev_time = now;
#endif

        printf("csv now:%.3f dt: %.3f s, accel: %.3f(%.03f), pressure: %.3f Pa, temp:%f\n", 
            now, dt, accel, ag, pressure, temp);
        if (default_hight == 0) {
          default_hight = calculate_altitude(pressure, temp);
          //self.ekf.x[0] = default_hight;
        }
        printf("pressure:%f -> high:%f ----> %f\n", pressure / 100, calculate_altitude(pressure, temp),  calculate_altitude(pressure, temp) - default_hight);

        dm_ekf_run_model(&self, dt, ag/*accel*/, pressure, temp);

        printf("Pv:%f, Pa:%f, Pp:%f\n", self.ekf.P[EKF_N + 1], self.ekf.P[EKF_N * 2 + 2], self.ekf.P[EKF_N * 3 + 3]);
        printf("dt: %.3f s, Height: %.3f m, Velocity: %.3f m/s, Acceleration: %.3f m/s², Delat Accel:%.3f\n",
               dt,
               self.ekf.x[0],
               self.ekf.x[1],
               self.ekf.x[2],
               self.ekf.x[3]);
#if 0        
        char cmd[256] = {0};
        // snprintf(cmd, sizeof(cmd), "echo %lf >> result.csv", self.ekf.x[2]);
        snprintf(cmd, sizeof(cmd), "echo %lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf,%lf >> result.csv", 
        self.ekf.x[2],self.ekf.P[EKF_N * 2 + 2],self.ekf.x[1],self.ekf.P[EKF_N * 1 + 1], self.ekf.x[0], 
        pressure,self.ekf.x[3], temp, self.ekf.x[4],
        calculate_altitude(self.ekf.x[3], self.ekf.x[4]), calculate_altitude(self.ekf.x[3], self.ekf.x[4]) * 300,
        calculate_altitude(pressure, temp));
        system(cmd);
        
        snprintf(cmd, sizeof(cmd), "echo %f,%f,%f >> result.P.csv", self.ekf.P[EKF_N + 1], self.ekf.P[EKF_N * 2 + 2], self.ekf.P[EKF_N * 3 + 3]);
        system(cmd);
#endif
        double altitude = calculate_altitude(pressure, temp);
        snprintf(buf, sizeof(buf), "%f,%f,%f,%f,%f,%f,%f,%f,%f\n", ag, self.ekf.x[2], self.ekf.x[1], self.ekf.x[0], pressure, temp, altitude, altitude - default_hight, self.ekf.x[3]);
        
        fwrite(buf, 1, strlen(buf), result_fd);
    }

    fclose(result_fd);
    fclose(file);
}
/*int main(int argc, char** argv) {

  if (argc < 2) {
    return -1;
  }
  process_csv(argv[1]);

  return 0;
}*/

#endif