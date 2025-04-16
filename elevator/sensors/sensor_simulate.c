#include <assert.h>
#include <iio.h>
#include <openssl/ossl_typ.h>
#include <stdint.h>
#include <string.h>
#include "file_util.h"
#include "hr_log.h"
#include "sensor.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define MAX_LINE_LENGTH 1000

static const char* SIMULATE_DATA_FILE = "simulate.csv";
static FILE* _simulate_data_fp = NULL;

static const int ACCELEROMETER_SAMPLE_RATE_HZ = 100;
// +-2G
static const double ACCELEROMETER_SCALE = 0.000598;

int64_t line_num = 1;
static int accelerometer_init() {
    int ret = 0;
    if (!_simulate_data_fp) {
        const char* path = SIMULATE_DATA_FILE;
        char line[MAX_LINE_LENGTH] = {0};
        FILE* fp = fopen(path, "r");
        if (!fp) {
            return -1;
        }

        fgets(line, MAX_LINE_LENGTH, fp);  // skip csv file head
        _simulate_data_fp = fp;
    }

    return 0;
}
static int accelerometer_configure(int sampling_rate) {
    // why sampling_frequency is not device's attribute?
    return 0;
}

static int accelerometer_read(struct sensor_data* data) {
    int i = 0;
    double now, dt, accel_x, accel_y, accel_z, accel_r, pressure, temp, ag;
    char* p = NULL;
    char line[MAX_LINE_LENGTH] = {0};
    struct sensor_data_accelerometer* sda = (struct sensor_data_accelerometer*)data;
    if (!sda || !_simulate_data_fp) {
        return -1;
    }

    sda->self.type = SENSOR_ACCELEROMETER;
    p = fgets(line, MAX_LINE_LENGTH, _simulate_data_fp);
    if (!p) {
        return -1;
    }

    if (sscanf(p, "%lf,%lf,%lf,%lf,%lf,%lf,%*f,%*f,%*f,%lf,%lf,%lf", &now, &dt, &accel_x,  &accel_y, &accel_z, &accel_r, &pressure, &temp, &ag) != 9) {
        printf("CSV 解析错误:%s\n", line);

        return -1;
    }
    
    sda->x[0] = accel_x;
    sda->x[1] = accel_y;
    sda->x[2] = accel_z;
    sda->dt = dt;

    line_num++;
    HR_LOGD("line:%ld, now:%f, accel: %f-%f-%f-%f\n", line_num, now, accel_x, accel_y, accel_z, accel_r);

    return 0;
}
    static int accelerometer_close() {
        if (_simulate_data_fp) {
            fclose(_simulate_data_fp);
            _simulate_data_fp = NULL;
        }
        return 0;
    }

    struct sensor_device sensor_simulate_accelerometer = {
        .init = accelerometer_init,
        .configure = accelerometer_configure,
        .read = accelerometer_read,
        .close = accelerometer_close};
