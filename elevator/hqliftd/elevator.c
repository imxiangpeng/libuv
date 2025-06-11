// mxp, 20250429, unified inteface for elevator

#include "elevator.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>

#include "platform.h"

static char _device_id[256] = {0};
static char _serial_no[256] = {0};
static char _mac[13] = {0};

int elevator_init(void) {

    return 0;
}
const char* elevator_serialno(void) {
    if (_serial_no[0] == '\0') {
        platform_get_property(PROPERTY_SERIAL, _serial_no, sizeof(_serial_no));
    }
    //    return _serial_no;
    return "LC4005CB4E213B8C9";//"244200000E480001";  //"AD00469RAG47506";// "E073E72988DA001";//"244200000E480001";
}

const char* elevator_mac(void) {
    if (_mac[0] == '\0') {
        size_t j = 0;
        char mac[18] = {0};

        platform_get_property(PROPERTY_MACADDR, mac, sizeof(mac));
        for (int i = 0; mac[i] != '\0' && j < sizeof(_mac) - 1; ++i) {
            if (mac[i] != ':') {
                _mac[j++] = toupper((unsigned char)mac[i]);
            }
        }
        _mac[j] = '\0';
    }
    return _mac;  //"D4430EF3063A";//"E073E72988DA";
}
const char* elevator_deviceid(void) {
    if (_device_id[0] == '\0') {
        platform_get_property(PROPERTY_DEVICEID, _device_id, sizeof(_device_id));
    }
    // return _device_id;
    return "GD500103000086";
}


double elevator_temperature(void) {
    double temp = 15.0;
    const char* temp_channel = "/sys/bus/iio/devices/iio:device0/in_temp_input";
    FILE* fp = fopen(temp_channel, "r");
    if (fp == NULL) {
        return 15.0f;
    }

    if (fscanf(fp, "%lf", &temp) != 1) {
        fclose(fp);
        return temp;
    }
    temp /= 1000.0f; // new bmp280 driver return value should / 1000
    temp = round(temp * 10) / 10;
    fclose(fp);
    return temp;
}

// houqi LiftState: lightVariationAmplitude 0-255
// in_voltage2_input [ 0 - 700 ]

static int map_range(int value, int in_min, int in_max, int out_min, int out_max) {
    if (value < in_min) value = in_min;
    if (value > in_max) value = in_max;
    return (value - in_min) * (out_max - out_min) / (in_max - in_min) + out_min;
}
int elevator_light_brightness() {
    int brightness = 100;  // 0-255
    const char* brightness_channel = "/sys/bus/iio/devices/iio:device1/in_voltage2_input";
    FILE* fp = fopen(brightness_channel, "r");
    if (fp == NULL) {
        return brightness;
    }

    if (fscanf(fp, "%d", &brightness) != 1) {
        fclose(fp);
        return brightness;
    }

    fclose(fp);

    // mxp, 20250610, 我们发现电梯中因为光源朝向问题我们检测到的亮度都比较低,
    // 在 A4 电梯中发现平常亮度为 150/170 左右，所以，我们就简单暴力的方案
    // 如果当前值大于 255 直接取 255， 否则直接使用当前值
    // round to nearest integer
    // brightness = (int)((brightness * 255) / 700);
    if (brightness >= 255) {
        brightness = 255;
    }

    printf("bright:%d\n", brightness);
    return brightness;
}