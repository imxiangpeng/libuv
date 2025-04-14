#include <stdio.h>
#include <unistd.h>
#include "tui.h"
#include "core.h"

int main(int argc, char** argv) {
    if (argc < 2) {
        // return -1;
    }
    // process_csv(argv[1]);

    if (0 != core_initalize(argc, argv)) {
        printf("error, can not initalize core ...\n");
        return -1;
    }

    tui_init();
    
    core_run();

    while (1) {
        sleep(2);
    }
    return 0;
}
