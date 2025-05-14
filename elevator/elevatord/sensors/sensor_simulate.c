
// mxp, support simulate sensor, you can capture data and store it in csv format
// then you can build with following command to enable simulate sensor:
// cmake -DCMAKE_EXPORT_COMPILE_COMMANDS=1 -DUSE_LOCAL_SIMULATE_DATA=1 -B build
// 

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

int64_t line_num = 1;

// 说明：这里仅仅是测试模拟数据，数据由 加速度所在任务以 100hz 频率读取
// 气压温度频率远低于该频率，将会使用 cache 数据
struct simulate_record {
    double now;
    double dt;
    double accel_x;
    double accel_y;
    double accel_z;
    double gyro_x;
    double gyro_y;
    double gyro_z;

    double pressure;
    double temperature;

    int accel_dirty;     // should read again
    int gyro_dirty;      // should read again
    int pressure_dirty;  // should read again
} _current_record;

// time,dt,accel_x,accel_y,accel_z,union_g,gyro_x,gyro_y,gyro_z,pressure,temp,ag,ag
static int simulate_record_read(struct simulate_record* record) {
    double accel_r;
    char* p = NULL;
    char line[MAX_LINE_LENGTH] = {0};
    if (!record) {
        return -1;
    }

    if (!_simulate_data_fp) {
        return -1;
    }

//read_again:
    p = fgets(line, MAX_LINE_LENGTH, _simulate_data_fp);
    if (!p) {
        if (feof(_simulate_data_fp)) {
            rewind(_simulate_data_fp);
            fgets(line, MAX_LINE_LENGTH, _simulate_data_fp);  // skip csv file head
            line_num = 1;
            // goto read_again;
        }
        return -1;
    }

    if (sscanf(p, "%lf,%lf,%lf,%lf,%lf,%lf,%*f,%*f,%*f,%lf,%lf",
               &record->now, &record->dt,
               &record->accel_x, &record->accel_y, &record->accel_z, &accel_r, &record->pressure, &record->temperature) != 8) {
        HR_LOGE("cvs parse error:%s\n", line);

        fclose(_simulate_data_fp);
        _simulate_data_fp = NULL;
        return -1;
    }

    record->accel_dirty = 0;
    record->gyro_dirty = 0;
    record->pressure_dirty = 0;

    line_num++;

    return 0;
}
static int accelerometer_init() {
    if (!_simulate_data_fp) {
        const char* path = SIMULATE_DATA_FILE;
        char line[MAX_LINE_LENGTH] = {0};
        FILE* fp = fopen(path, "r");
        if (!fp) {
            HR_LOGE("can not open simulate data file\n");
            return -1;
        }

        fgets(line, MAX_LINE_LENGTH, fp);  // skip csv file head
        _simulate_data_fp = fp;
    }

    return 0;
}
static int accelerometer_configure(double sampling_rate) {
    (void)sampling_rate;
    // why sampling_frequency is not device's attribute?
    return 0;
}

static int accelerometer_read(struct sensor_data* data) {
    struct sensor_data_accelerometer* sda = (struct sensor_data_accelerometer*)data;
    if (!sda || !_simulate_data_fp) {
        return -1;
    }

    if (0 != simulate_record_read(&_current_record)) {
        return -1;
    }

    sda->self.type = SENSOR_ACCELEROMETER;

    sda->x[0] = _current_record.accel_x;
    sda->x[1] = _current_record.accel_y;
    // -1 correct data direction
    // because simulate data is record while device is inverted/upside down
    sda->x[2] = _current_record.accel_z /** -1.0*/;
    sda->dt = _current_record.dt;

    HR_LOGD("line:%ld, now:%f(%f), accel: %f-%f-%f, pressure:%f, temp:%f\n", line_num, _current_record.now, _current_record.dt, _current_record.accel_x,
         _current_record.accel_y, _current_record.accel_z, _current_record.pressure, _current_record.temperature);

    return 0;
}
static int accelerometer_close() {
    if (_simulate_data_fp) {
        fclose(_simulate_data_fp);
        _simulate_data_fp = NULL;
    }
    return 0;
}

struct sensor sensor_simulate_accelerometer = {
    .init = accelerometer_init,
    .configure = accelerometer_configure,
    .read = accelerometer_read,
    .close = accelerometer_close};

static int barometer_init() {
    return 0;
}
static int barometer_configure(double sampling_rate) {
    (void)sampling_rate;
    // why sampling_frequency is not device's attribute?
    return 0;
}

static int barometer_read(struct sensor_data* data) {
    struct sensor_data_barometer* sdb = (struct sensor_data_barometer*)data;
    if (!sdb || !_simulate_data_fp) {
        return -1;
    }

    // use cached data
    sdb->self.type = SENSOR_BAROMETER;

    sdb->pressure = _current_record.pressure;
    sdb->temperature = _current_record.temperature;

    return 0;
}
static int barometer_close() {
    return 0;
}

struct sensor sensor_simulate_barometer = {
    .init = barometer_init,
    .configure = barometer_configure,
    .read = barometer_read,
    .close = barometer_close};
