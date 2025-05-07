// mxp, 20250429, unified inteface for elevator

#include "elevator.h"

#include <ctype.h>
#include <stdio.h>

#include "platform.h"

static char _device_id[256] = {0};
static char _serial_no[256] = {0};
static char _mac[13] = {0};
int elevator_init(void) {
    size_t j = 0;
    char mac[18] = {0};
    
    platform_get_property(PROPERTY_MACADDR, mac, sizeof(mac));
    for (int i = 0; mac[i] != '\0' && j < sizeof(_mac) - 1; ++i) {
        if (mac[i] != ':') {
            _mac[j++] = toupper((unsigned char)mac[i]);
        }
    }
    _mac[j] = '\0';

    platform_get_property(PROPERTY_DEVICEID, _device_id, sizeof(_device_id));
    platform_get_property(PROPERTY_SERIAL, _serial_no, sizeof(_serial_no));

    return 0;
}
const char* elevator_serialno(void) {
    //    return _serial_no;
    return "244200000E480001";//"AD00469RAG47506";// "E073E72988DA001";//"244200000E480001";
}

const char* elevator_mac(void) {
    
    return _mac;//"D4430EF3063A";//"E073E72988DA";
}
const char* elevator_deviceid(void) {
    // return _device_id;
    return "GD500103000086";
}
int elevator_get_status(struct elevator_status *st) {
    if (!st) {
        return -1;
    }
    

    return 0;
}
int elevator_direction(void) {
    return ELEVATOR_DIR_STATIONARY;
}
int elevator_floor(void) {
    return 10;
}
float elevator_speed(void) {
    return 0.5f;
}
float elevator_travel_distance(void) {
    return  10.f;
}
int elevator_door(void) {
    return 1;
}
int elevator_passenger_count(void) {
    return 6;
}
int elevator_passenger_count_in(void) {
    return 2;
}
int elevator_passenger_count_out(void) {
    return 1;
}

float elevator_temperature(void) {
    float temp = 15.0;
    const char* temp_channel = "/sys/bus/iio/devices/iio:device0/in_temp_input";
    FILE *fp = fopen(temp_channel, "r");
    if (fp == NULL) {
        return 15.0f;
    }

    if (fscanf(fp, "%f", &temp) != 1) {
        fclose(fp);
        return temp;
    }

    fclose(fp);
    return temp;
}

int elevator_light_variant_amplitude() {
    return 30;
}