
// mxp, support bmi270 imu sensor which provide accelerometer and gyroscope

#include <assert.h>
#include <iio.h>

#include <string.h>
#include "file_util.h"
#include "hr_log.h"
#include "sensor.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static const int ACCELEROMETER_SAMPLE_RATE_HZ = 100;
// +-2G
static const double ACCELEROMETER_SCALE = 0.000598;

static const char* IIO_BMI270_NAME = "bmi270";
static const char* IIO_ACCEL_CHS[IMU_AXES] = {"accel_x", "accel_y", "accel_z"};
static const char* IIO_GYRO_CHS[IMU_AXES] = {"anglvel_x", "anglvel_y", "anglvel_z"};

static struct iio_device* _bmi270 = NULL;

static int bmi270_init() {
    struct iio_channel* ch = NULL;
    if (!_bmi270) {
        struct iio_context* ctx = iio_create_default_context();
        if (ctx) {
            _bmi270 = iio_context_find_device(ctx, IIO_BMI270_NAME);
        }
    }

    if (!_bmi270) {
        HR_LOGE("%s(%d): can not found: %s\n", __FUNCTION__, __LINE__, IIO_BMI270_NAME);
        return -1;
    }

    // why sampling_frequency is not device's attribute?

    ch = iio_device_find_channel(_bmi270, IIO_ACCEL_CHS[0], false);
    if (!ch) {
        HR_LOGE("%s(%d): can not found: %s\n", __FUNCTION__, __LINE__, IIO_ACCEL_CHS[0]);
        return -1;
    }

    iio_channel_attr_write_double(ch, "sampling_frequency", ACCELEROMETER_SAMPLE_RATE_HZ);
    iio_channel_attr_write_double(ch, "scale", ACCELEROMETER_SCALE);

    return 0;
}
static int accelerometer_init() {
    if (bmi270_init() != 0) {
        HR_LOGE("%s(%d): accelerometer init failed\n", __FUNCTION__, __LINE__);
        return -1;
    }
    return 0;
}
static int accelerometer_configure(double sampling_rate) {
    struct iio_channel* ch = NULL;

    if (!_bmi270) {
        return -1;
    }

    // why sampling_frequency is not device's attribute?

    ch = iio_device_find_channel(_bmi270, IIO_ACCEL_CHS[0], false);
    if (!ch) {
        return -1;
    }

    iio_channel_attr_write_double(ch, "sampling_frequency", sampling_rate);

    return 0;
}

static int accelerometer_read(struct sensor_data* data) {
    size_t i = 0;
    struct iio_channel* ch = NULL;
    struct sensor_data_accelerometer* accel = (struct sensor_data_accelerometer*)data;
    if (!accel || !_bmi270) {
        return -1;
    }

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
static int gyroscope_configure(double sampling_rate) {
    (void)sampling_rate;
    return 0;
}

static int gyroscope_read(struct sensor_data* data) {
    size_t i = 0;
    struct iio_channel* ch = NULL;
    struct sensor_data_gyroscope* gyro = (struct sensor_data_gyroscope*)data;

    if (!gyro || !_bmi270) {
        return -1;
    }

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

struct sensor sensor_bmi270_accelerometer = {
    .init = accelerometer_init,
    .configure = accelerometer_configure,
    .read = accelerometer_read,
    .close = accelerometer_close};
struct sensor sensor_bmi270_gyroscope = {
    .init = gyroscope_init,
    .configure = gyroscope_configure,
    .read = gyroscope_read,
    .close = gyroscope_close};
