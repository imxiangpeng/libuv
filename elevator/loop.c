#include <uv.h>

int loop_prepare(void) {

    iot_init();
    return 0;
}

int loop_run() {

    return 0;
}

int loop_stop() {
    
    return 0;
}