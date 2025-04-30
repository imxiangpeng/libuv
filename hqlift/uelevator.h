#ifndef ELEVATOR_UBUS_H
#define ELEVATOR_UBUS_H
#include "elevator.h"
int elevator_ubus_init(void);
int elevator_ubus_deinit(void);

int uelevator_get_status(struct elevator_status *st);
#endif