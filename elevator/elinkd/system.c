// mxp, 20250710, system implement

#include "system.h"

#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "platform.h"

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

// do not use platform.h, both defined PROPERTY_SERIAL
int system_property_serial(struct property* self) {
    static char _serial[256] = {0};
    if (!self) return -1;

    platform_get_property(PROPERTY_SERIAL, _serial, sizeof(_serial));

    property_value_set_string_ext(&self->value, _serial, 1);
    return 0;
}

int system_property_build_timestamp(struct property* self) {
    (void)self;
    if (!self) return -1;

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

    return 0;
}

int system_service_reboot() {
    system("sync;reboot");
    return 0;
}
int system_service_start_ssh_tunnel() {
    system("/etc/init.d/ssh_tunnel restart");
    return 0;
}

int system_service_stop_ssh_tunnel() {
    system("/etc/init.d/ssh_tunnel stop");
    return 0;
}
