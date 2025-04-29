#ifndef DM_IMPL_H
#define DM_IMPL_H

typedef enum {
    PROPERTY_SERIAL = 0,
    PROPERTY_MACADDR,
    PROPERTY_DEVICEID,
    _PROPERTY_MAX
} PROPERTY_e;

// client should implement interface
int dm_impl_system_property_get(PROPERTY_e which, char* data, int len);


// running information provided by dm_main
const char* dm_running_interface_macaddr();
const char* dm_running_interface_ipv4addr();

#endif // DM_IMPL_