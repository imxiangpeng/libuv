#ifndef UELEVATOR_H
#define UELEVATOR_H
#include "elevator.h"
int uelevator_init(void);
int uelevator_deinit(void);

int uelevator_get_status(struct elevator_status *st);
int uelevator_get_historical(struct elevator_historical **h);
#endif