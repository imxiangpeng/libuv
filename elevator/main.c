#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <uv.h>
#include "floor.h"
#include "hr_log.h"
#include "iot/iot.h"
#include "motion.h"
#include "sensor.h"
#include "tui.h"

int main(int argc, char** argv) {
    int is_calibration = 0;
    int base_floor = 1;
    int floors_below_base = 0;
    int floors_above_base = 0;

    argc--;
    argv++;

    while (argc > 0) {
        printf("argv:%s\n", argv[0]);
        if (!strcmp(argv[0], "calibration")) {
            if (argc < 4) {
                HR_LOGE("invalid parameter for calibration\n");
                return -1;
            }

            is_calibration = 1;

            base_floor = atoi(argv[1]);
            floors_below_base = atoi(argv[2]);
            floors_above_base = atoi(argv[3]);

            printf("base:%d, floors below:%d, above:%d\n", base_floor, floors_below_base, floors_above_base);

            argc -= 3;
            argv += 3;
        }

        printf("argc:%d\n", argc);
        argc--;
        argv++;
    }

    printf("is_calibration:%d\n", is_calibration);
    sensor_init();

    if (0 != motion_initalize(argc, argv)) {
        printf("error, can not initalize core ...\n");
        return -1;
    }

    floor_init();
 
    if (is_calibration) {
        floor_enter_calibration(base_floor, floors_below_base, floors_above_base);
    }   
    // tui_init();

    iot_init();

    //motion_run();

    iot_run(uv_default_loop());
    uv_run(uv_default_loop(), UV_RUN_DEFAULT);

    iot_finally();
    uv_loop_close(uv_default_loop());
    return 0;
}
