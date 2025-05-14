// mxp, support bmp388 barometer sensor

#include <iio.h>

#include "hr_log.h"
#include "sensor.h"

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static const char* IIO_BMP388_NAME = "bmp380";
static const char* IIO_CH_PRESSURE_NAME = "pressure";
static const char* IIO_CH_TEMPERATURE_NAME = "temp";

static struct iio_device* _bmp388 = NULL;

static int bmp388_init() {
    if (!_bmp388) {
        struct iio_context* ctx = iio_create_default_context();
        if (ctx) {
            _bmp388 = iio_context_find_device(ctx, IIO_BMP388_NAME);
        }
    }

    if (!_bmp388)
        return -1;

    return 0;
}
static int barometer_init() {
    return bmp388_init();
}
static int barometer_configure(double sampling_rate) {
    (void)sampling_rate;

    iio_device_attr_write_double(_bmp388, "sampling_frequency", sampling_rate /*12.5*/);
    return 0;
}

static int barometer_read(struct sensor_data* data) {
    struct iio_channel* ch = NULL;
    struct sensor_data_barometer* baro = (struct sensor_data_barometer*)data;

    if (!baro)
        return -1;

    baro->self.type = SENSOR_BAROMETER;
    ch = iio_device_find_channel(_bmp388, IIO_CH_PRESSURE_NAME, false);
    if (!ch) {
        return -1;
    }

    if (iio_channel_attr_read_double(ch, "input", &baro->pressure) != 0) {
        HR_LOGE("%s(%d): failed to read %s\n", __FUNCTION__, __LINE__, IIO_CH_PRESSURE_NAME);
        return -1;
    }

    ch = iio_device_find_channel(_bmp388, IIO_CH_TEMPERATURE_NAME, false);
    if (!ch) {
        return -1;
    }

    if (iio_channel_attr_read_double(ch, "input", &baro->temperature) != 0) {
        HR_LOGE("%s(%d): failed to read %s\n", __FUNCTION__, __LINE__, IIO_CH_PRESSURE_NAME);
        return -1;
    }

    return 0;
}

static int barometer_close() {
    return 0;
}

static int temperature_init() {
    return bmp388_init();
}
static int termperature_configure(double sampling_rate) {
    (void)sampling_rate;
    return 0;
}

static int termperature_read(struct sensor_data* data) {
    struct iio_channel* ch = NULL;
    struct sensor_data_temperature* baro = (struct sensor_data_temperature*)data;

    if (!baro)
        return -1;

    baro->self.type = SENSOR_TEMPERATURE;
    ch = iio_device_find_channel(_bmp388, IIO_CH_TEMPERATURE_NAME, false);
    if (!ch) {
        return -1;
    }

    if (iio_channel_attr_read_double(ch, "input", &baro->temperature) != 0) {
        HR_LOGE("%s(%d): failed to read %s\n", __FUNCTION__, __LINE__, IIO_CH_PRESSURE_NAME);
        return -1;
    }

    return 0;
}

static int termperature_close() {
    return 0;
}

struct sensor sensor_bmp388_barometer = {
    .init = barometer_init,
    .configure = barometer_configure,
    .read = barometer_read,
    .close = barometer_close};

struct sensor sensor_bmp388_temperature = {
    .init = temperature_init,
    .configure = termperature_configure,
    .read = termperature_read,
    .close = termperature_close};