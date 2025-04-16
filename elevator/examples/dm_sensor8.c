#include <dirent.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <iio.h>
#include <time.h>
#include <unistd.h>
#include <errno.h>

#include "hr_log.h"

static const float G = 9.81;

#define EKF_N 2
#define EKF_M 1 // accelaration xyz


#define _float_t double
#include <tinyekf.h>

struct dm_sensor {
    ekf_t ekf;
};

#define TIME_STEP                                                              \
  (1.0 / 100.0) // \Delta t, 100ms采样间隔, 对应 100Hz sample rate
static const double Q [EKF_N * EKF_N] = {
    1e-4f, 0,
    0, 1e-4f
};

static double R[EKF_M * EKF_M] = {
    0.001
};

static double F[EKF_N * EKF_N] = {
    1, TIME_STEP,
    0, 1
};
static const float H[EKF_N] = {
   0.0, 1.0
};

static float P[EKF_N * EKF_N] = {1, 0, 0, 1};

// seconds
static double get_system_clock() {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec + ts.tv_nsec * 1e-9;
}

#define MOVING_AVG_FILTER_WINDOW (3)

struct moving_avg_filter {
  double data[MOVING_AVG_FILTER_WINDOW]; // data
  int index;                            // current index
  int size;                             // valid element size
  double sum;
};


double moving_average_filter(struct moving_avg_filter *f, double val) {
  if (!f)
    return 0;

  // window full
  // remove old value from sum
  if (f->size == MOVING_AVG_FILTER_WINDOW ) {
    f->sum -= f->data[f->index];
  }
  f->data[f->index] = val;
  f->sum += val;
  f->index = (f->index + 1) % MOVING_AVG_FILTER_WINDOW; // circle buffer
  if (f->size != MOVING_AVG_FILTER_WINDOW) {
    f->size++;
  }

  return f->sum / f->size;
}
static double read_channel_attr(struct iio_device *imu, const char *channel_name, const char* attr) {
  struct iio_channel *ch = iio_device_find_channel(imu, channel_name, false);
  if (!ch) {
    fprintf(stderr, "Error: Channel %s not found\n", channel_name);
    exit(1);
  }

  double value = 0;
  if (iio_channel_attr_read_double(ch, attr, &value) < 0) {
    fprintf(stderr, "Error: Failed to read %s\n", channel_name);
    exit(1);
  }
  return value;
}

static double read_channel(struct iio_device *imu, const char *channel_name) {
  struct iio_channel *ch = iio_device_find_channel(imu, channel_name, false);
  if (!ch) {
    fprintf(stderr, "Error: Channel %s not found\n", channel_name);
    exit(1);
  }

  double value, scale;
  if (iio_channel_attr_read_double(ch, "raw", &value) < 0) {
    fprintf(stderr, "Error: Failed to read %s\n", channel_name);
    exit(1);
  }
  iio_channel_attr_read_double(ch, "scale", &scale);
  return value * scale;
}


static double low_pass_filter(double input, double previous_output, double alpha) {
  return alpha * input + (1 - alpha) * previous_output;
}
static void run_model(ekf_t * ekf, 
    const double val[EKF_M], 
    double fx[EKF_N],
    double F[EKF_N * EKF_N],
    double hx[EKF_M],
    double H[EKF_M*EKF_N],
    double delta_t) {

    // velocity
    fx[0] = ekf->x[0] + ekf->x[1] * delta_t;
    // acceleration
    fx[1] = ekf->x[1];
    
    // F[0,0]
    F[0] = 1;
    // F[0,1]
    F[1] = delta_t;
    // F[1,0]
    F[3] = 0;
    // F[1,1]
    F[4] = 1;

    H[0] = 0;
    H[1] = 1;
    
    // hx[0] = ekf->x[0];
    hx[0] = ekf->x[1];
    // hx[0] = low_pass_filter(val[0], ekf->x[1], 0.8);
}
static double _pressure = 0;
static double _temp = 0;
static void * _routin(void* argv) {
  struct iio_context *ctx = iio_create_default_context();
  struct iio_device *bmp388 = iio_context_find_device(ctx, "bmp388");
  while (1) {
    
      double pressure = read_channel_attr(bmp388, "pressure", "input");
      double temp = read_channel_attr(bmp388, "temp", "input");
      
      _pressure = pressure;
      _temp = temp;

      //printf("pressure:%f, temp:%f\n", _pressure, _temp);
      usleep( 1000 * 50);
  }
}
int main(int argc, char** argv) {
  
  setvbuf(stdout, NULL, _IOLBF, 0);
    struct dm_sensor sensor;
    memset((void*)&sensor, 0, sizeof(sensor));

    const _float_t pdiag[EKF_N] = {1.0, 1.0};
    ekf_initialize(&sensor.ekf, pdiag);
    sensor.ekf.x[0] = 0;
    sensor.ekf.x[1] = G;
    
  struct moving_avg_filter filter;
  memset((void *)&filter, 0, sizeof(filter));

    struct iio_context *ctx = iio_create_default_context();
    struct iio_device *dev = iio_context_find_device(ctx, "bmi270");
    struct iio_device *bmp388 = iio_context_find_device(ctx, "bmp388");

    double begin_time = get_system_clock();
    double last_time = get_system_clock();
    double delta_t = 0;
  float speed = 0.0f;
  double dist = 0.0;
  
  pthread_t pid;
  pthread_create(&pid, NULL, _routin, NULL);
 
    while(1) {

      double now_time = get_system_clock();
      delta_t = now_time - last_time;
      last_time = now_time;
      double now_time_caiyang = get_system_clock();
      double accel_x = read_channel(dev, "accel_x");
      double accel_y = read_channel(dev, "accel_y");
      double accel_z = read_channel(dev, "accel_z");
      double gyro_x = read_channel(dev, "anglvel_x");
      double gyro_y = read_channel(dev, "anglvel_y");
      double gyro_z = read_channel(dev, "anglvel_z");

      double pressure = _pressure;// read_channel_attr(bmp388, "pressure", "input");
      double temp = _temp;//read_channel_attr(bmp388, "temp", "input");
      double accel_union_z = sqrt(accel_x * accel_x + accel_y * accel_y + accel_z * accel_z);
     
      accel_union_z = accel_union_z * (accel_z < 0 ? -1.0:1);
      //printf("mengxp -> time:%f(%f), accel_x:%f, accel_y:%f, accel_z:%f, union_g:%f,gyro_x:%f, gyro_y:%f, gyro_z:%f, pressure:%f, temp:%f\n", 
      //now_time - begin_time, delta_t, accel_x, accel_y, accel_z, accel_union_z, gyro_x, gyro_y, gyro_z, pressure, temp);
      
      HR_LOGD("mengxp -> time:%f(%f), accel_x:%f, accel_y:%f, accel_z:%f, union_g:%f,gyro_x:%f, gyro_y:%f, gyro_z:%f, pressure:%f, temp:%f\n", 
      now_time - begin_time, delta_t, accel_x, accel_y, accel_z, accel_union_z, gyro_x, gyro_y, gyro_z, pressure, temp);
      delta_t = TIME_STEP - (get_system_clock() - now_time_caiyang);
#if 0
      // Run our model to get the EKF inputs
      double val[EKF_M]; 
      double fx[EKF_N];
      double F[EKF_N * EKF_N];
      double hx[EKF_M];
      double H[EKF_M*EKF_N] = {0};
      double value[EKF_M] ={accel_union_z};
      
      run_model(&sensor.ekf, value, fx, F, hx, H, delta_t);

      printf("before predict v:%f, a:%f\n", sensor.ekf.x[0], sensor.ekf.x[1]);
      printf("before predict h:%f\n", hx[0]);
      ekf_predict(&sensor.ekf, fx, F, Q);
      
      printf("after predict v:%f, a:%f\n", sensor.ekf.x[0], sensor.ekf.x[1]);
      printf("after predict h:%f\n", hx[0]);
      // Run the EKF update step
      double z[EKF_M] = {accel_union_z};
      ekf_update(&sensor.ekf, z, hx, H, R); 
      
      printf("delta t:%f\n", delta_t);
      printf("celiang: av:%f\n", accel_union_z);
      printf("h:%f\n", hx[0]);
      printf("v:%f, a:%f\n", sensor.ekf.x[0], sensor.ekf.x[1]);
      printf("g0:%f\n", accel_union_z);
      printf("g1:%f\n", sensor.ekf.x[1]);

      double g3 = moving_average_filter(&filter, accel_union_z );
      printf("g3:%f\n", g3);

      g3 = round(g3 *1000)/1000;
      g3 -= 9.81;
      if (g3 > 0.02) {
      delta_t = round(delta_t * 100)/100.0;
      dist += speed * delta_t + 0.5 * g3 * delta_t * delta_t;
      speed += g3 * delta_t;
      }     
      printf("delta:%f, a:%f, speed:%f, dist:%f\n", delta_t, g3, speed , dist);
#endif      
      usleep(1000 * 1000 * delta_t);
    }
    
    return 0;
}
