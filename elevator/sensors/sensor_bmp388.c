#include <iio.h>

#include "sensor.h"

static struct iio_device* _bmp388 = NULL;

static int barometer_init() {
    if (!_bmp388)
        _bmp388 = iio_context_find_device(iio_create_default_context(), "bmp388");

    return _bmp388 ? 0 : -1;
}
static int barometer_configure(int sampling_rate) {
    return 0;
}

static int barometer_read(struct sensor_data* data) {
    return 0;
}

static int barometer_close() {
    return 0;
}
struct sensor_device sensor_barometer_bmp388 = {
    .init = barometer_init,
    .configure = barometer_configure,
    .read = barometer_read,
    .close = barometer_close};