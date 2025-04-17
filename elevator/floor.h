#ifndef FLOOR_H
#define FLOOR_H
int floor_init();

int floor_predict(double height, int *num, char* label, int length);

int floor_relative_height(int num, double* height);

int floor_enter_calibration(int floors_under_base, int base_floor, int floors_max);
#endif