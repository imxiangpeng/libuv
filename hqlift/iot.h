#ifndef IOT_H
#define IOT_H

#include <uv.h>

int iot_init(uv_loop_t* loop);

int iot_deinit();
#endif