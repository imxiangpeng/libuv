#include <assert.h>
#include <iio.h>

#include "hr_log.h"
#include "sensor.h"

static const char *IIO_BMI270_NAME = "bmi270";
static const char *IIO_ACCEL_CHS[IMU_AXES] = {"accel_x, accel_y, accel_z"};
static const char *IIO_GYRO_CHS[IMU_AXES] = {"anglvel_x", "anglvel_y", "anglvel_z"};

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static struct iio_device *_bmi270 = NULL;
static int bmi270_init() {
    if (!_bmi270)
        _bmi270 = iio_context_find_device(iio_create_default_context(), IIO_BMI270_NAME);

    if (!_bmi270)
        return -1;

    return 0;
}
static int accelerometer_init() {
    return bmi270_init();
}
static int accelerometer_configure(int sampling_rate) {
    return 0;
}

static int accelerometer_read(struct sensor_data *data) {
    int i = 0;
    double *p = NULL;
    struct iio_channel *ch = NULL;
    struct sensor_data_accelerometer *accel = (struct sensor_data_accelerometer *)data;
    if (!accel)
        return -1;

    accel->self.type = SENSOR_ACCELEROMETER;

    assert(ARRAY_SIZE(IIO_ACCEL_CHS) == ARRAY_SIZE(accel->x));

    for (i = 0; i < ARRAY_SIZE(IIO_ACCEL_CHS); i++) {
        double scale = 0;
        ch = iio_device_find_channel(_bmi270, IIO_ACCEL_CHS[i], false);
        if (!ch) {
            return -1;
        }

        if (iio_channel_attr_read_double(ch, "raw", &accel->x[i]) != 0) {
            HR_LOGE("%s(%d): failed to read channel: %s\n", __FUNCTION__, __LINE__, IIO_ACCEL_CHS[i]);
            return -1;
        }

        if (iio_channel_attr_read_double(ch, "scale", &scale) != 0) {
            HR_LOGE("%s(%d): failed to read channel: %s\n", __FUNCTION__, __LINE__, IIO_ACCEL_CHS[i]);
            return -1;
        }

        accel->x[i] *= scale;
    }

    return 0;
}

static int accelerometer_close() {
    return 0;
}

static int gyroscope_init() {
    return bmi270_init();
}
static int gyroscope_configure(int sampling_rate) {
    return 0;
}

static int gyroscope_read(struct sensor_data *data) {
    int i = 0;
    double *p = NULL;
    struct iio_channel *ch = NULL;
    struct sensor_data_gyroscope *gyro = (struct sensor_data_gyroscope *)data;

    if (!gyro)
        return -1;

    gyro->self.type = SENSOR_GYROSCOPE;

    assert(ARRAY_SIZE(IIO_GYRO_CHS) == ARRAY_SIZE(gyro->x));

    for (i = 0; i < ARRAY_SIZE(IIO_GYRO_CHS); i++) {
        double scale = 0;
        ch = iio_device_find_channel(_bmi270, IIO_GYRO_CHS[i], false);
        if (!ch) {
            return -1;
        }

        if (iio_channel_attr_read_double(ch, "raw", &gyro->x[i]) != 0) {
            HR_LOGE("%s(%d): failed to read channel: %s\n", __FUNCTION__, __LINE__, IIO_GYRO_CHS[i]);
            return -1;
        }

        if (iio_channel_attr_read_double(ch, "scale", &scale) != 0) {
            HR_LOGE("%s(%d): failed to read channel: %s\n", __FUNCTION__, __LINE__, IIO_GYRO_CHS[i]);
            return -1;
        }

        gyro->x[i] *= scale;
    }

    return 0;
}

static int gyroscope_close() {
    return 0;
}

struct sensor_device sensor_bmi270_accelerometer = {
    .init = accelerometer_init,
    .configure = accelerometer_configure,
    .read = accelerometer_read,
    .close = accelerometer_close};
struct sensor_device sensor_bmi270_gyroscope = {
    .init = gyroscope_init,
    .configure = gyroscope_configure,
    .read = gyroscope_read,
    .close = gyroscope_close};