#include <stdio.h>
#include <unistd.h>
#include "tui.h"
#include "core.h"
#include "sensor.h"
#include "floor.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        // return -1;
    }
    // process_csv(argv[1]);

    sensor_manager_init();

    if (0 != core_initalize(argc, argv)) {
        printf("error, can not initalize core ...\n");
        return -1;
    }

    floor_init();
    tui_init();
    
    //return 0;;
    core_run();

    while (1) {
        sleep(2);
    }
    return 0;
}
