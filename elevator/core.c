#include <stdio.h>
#include <stdlib.h>
#include <string.h>


#include "acceleration.h"
#include "barometer.h"

int core_initalize(void) {

    acceleration_initialize();
    barometer_initialize();
    return 0;
}