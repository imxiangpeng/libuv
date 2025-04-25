#ifndef IOT_H
#define IOT_H

#include <uv.h>

struct iot {
    char* id;
    char* server;
    int port;
    int alive_time;
    char* username;
    char* password;
};
int iot_init();

int iot_run(uv_loop_t* loop);

int iot_finally();
#endif