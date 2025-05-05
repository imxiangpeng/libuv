#ifndef PLATFORM_H
#define PLATFORM_H
#include <stddef.h>

typedef enum {
    PROPERTY_SERIAL = 0,
    PROPERTY_MACADDR,
    PROPERTY_DEVICEID,
    _PROPERTY_MAX
} PROPERTY_e;

// client should implement interface
int platform_get_property(PROPERTY_e which, char* data, int len);
#endif // DM_IMPL_