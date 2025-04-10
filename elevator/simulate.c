#include <stdio.h>

#include "simulate.h"


#define MAX_LINE_LENGTH 1000
// simulate using local csv files
//#define USE_LOCAL_SIMULATE_DATA 1

static const char *SIMULATE_DATA_FILE = "simulate.csv";
static FILE *_simulate_data_fp = NULL;

int simulate_data_init(const char* path) {
    if (!path) {
        path = SIMULATE_DATA_FILE;
    }
    FILE *fp = fopen(path, "r");
    if (!fp) return -1;

    char line[MAX_LINE_LENGTH] = {0};
    fgets(line, MAX_LINE_LENGTH, fp);  // skip csv file head
    _simulate_data_fp = fp;

    return 0;
}

int simulate_data_read(struct simulate_data *data) {
    double now, dt, accel, pressure, temp, ag;
    char line[MAX_LINE_LENGTH];
    if (!data || !_simulate_data_fp)
        return -1;

    char *p = fgets(line, MAX_LINE_LENGTH, _simulate_data_fp);
    if (!p) return -1;

    if (sscanf(p, "%lf,%lf,%*f,%*f,%*f,%lf,%*f,%*f,%*f,%lf,%lf,%lf", &now, &dt, &accel, &pressure, &temp, &ag) != 6) {
        printf("CSV 解析错误:%s\n", line);
    }

    data->accel_x = 0;
    data->accel_y = 0;
    data->accel_z = accel;
    data->now = now;
    data->pressure = pressure;
    data->temp = temp;
    return 0;
}

int simulate_data_deinit(void) {
    if (!_simulate_data_fp) return 0;

    fclose(_simulate_data_fp);
    _simulate_data_fp = NULL;
    return 0;
}