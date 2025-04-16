#include "sensor.h"
#include <stddef.h>

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

extern struct sensor_device sensor_bmi270_accelerometer;
extern struct sensor_device sensor_bmi270_gyroscope;

extern struct sensor_device sensor_bmp388_barometer;
extern struct sensor_device sensor_bmp388_temperature;

#if USE_LOCAL_SIMULATE_DATA
extern struct sensor_device sensor_simulate_accelerometer;
#endif

static struct sensor_device* _sensor_devices[_SENSOR_MAX] = {
    [SENSOR_ACCELEROMETER] = &sensor_bmi270_accelerometer,
    [SENSOR_GYROSCOPE] = &sensor_bmi270_gyroscope,
    [SENSOR_BAROMETER] = &sensor_bmp388_barometer,
    [SENSOR_TEMPERATURE] = &sensor_bmp388_temperature,
};

int sensor_manager_init() {
    int i = 0;
#if USE_LOCAL_SIMULATE_DATA
    _sensor_devices[SENSOR_ACCELEROMETER] = &sensor_simulate_accelerometer;
#endif
    for (i = 0; i < _SENSOR_MAX; i++) {
        struct sensor_device* sensor = sensor_manager_get_device(SENSOR_ACCELEROMETER);
        if (!sensor) {
            return -1;
        }
        sensor->init();
    }

    return 0;
}

struct sensor_device* sensor_manager_get_device(enum sensor type) {
    if (type < 0 | type > _SENSOR_MAX - 1) {
        return NULL;
    }
    return _sensor_devices[type];
}
