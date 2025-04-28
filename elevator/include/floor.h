#ifndef FLOOR_H
#define FLOOR_H
int floor_init();
int floor_deinit();

int floor_predict(double height, int *num, char* label, int length);

int floor_relative_height(int num, double* height);

int floor_enter_calibration(int base_floor, int floors_below_base, int floors_above_base);

typedef void (*floor_calibration_cb)(int id, int floor, const char* label, double height);
int floor_enter_calibration_with_callback(int base_floor, int floors_below_base, int floors_above_base,floor_calibration_cb cb);
#endif