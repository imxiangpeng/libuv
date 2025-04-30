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

int platform_get_mac_address(char *mac, size_t size);
int platform_get_ip_address(char *ip, size_t size);
const char* platform_get_connection_mac_address(void);
int platform_set_connection_mac_address(char* data);
const char* platform_get_connection_ipv4_address(void);
int platform_set_connection_ipv4_address(char* data);
#endif // DM_IMPL_