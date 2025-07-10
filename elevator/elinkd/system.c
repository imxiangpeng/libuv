#include "system.h"

#include <stdio.h>
#include <string.h>

#include "property.h"
// #include "topic_property.h"

static char _sw_version[64] = {0};

static void read_sw_version(void) {
    char* eol = NULL;
    FILE* fp = NULL;

    fp = fopen("/etc/sw-versions", "r");
    if (!fp) {
        return;
    }

    fgets(_sw_version, sizeof(_sw_version), fp);

    eol = strchr(_sw_version, '\n');
    if (eol) *eol = '\0';

    fclose(fp);
}

int system_property_build_timestamp(struct property* self) {
    (void)self;
    if (!self) return -1;

    printf("%s(%d): ............:%s.........\n", __FUNCTION__, __LINE__, BUILD_TIMESTAMP);
    // do not free
    property_value_set_string_ext(&self->value, BUILD_TIMESTAMP, 1);
    return 0;
}

int system_property_sw_version(struct property* self) {
    (void)self;

    if (_sw_version[0] == '\0') {
        read_sw_version();
    }
    property_value_set_string_ext(&self->value, _sw_version, 1);

    // topic_property_report();
    return 0;
}
