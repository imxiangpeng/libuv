#include <errno.h>
#include <fcntl.h>
#include <mosquitto.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "dm_impl.h"

#define UNIFYKEYS_CDEV "/dev/unifykeys"

#define KEYUNIFY_ATTACH _IO('f', 0x60)
#define KEYUNIFY_GET_INFO _IO('f', 0x62)

#define KEY_UNIFY_NAME_LEN 48

/* for ioctrl transfer paramters. */
struct key_item_info_t {
    unsigned int id;
    char name[KEY_UNIFY_NAME_LEN];
    unsigned int size;
    unsigned int permit;
    unsigned int flag; /*bit 0: 1 exsit, 0-none;*/
    unsigned int reserve;
};

static char* _prop_tbl[_PROPERTY_MAX] = {
    "usid", // serial
    "mac", // macaddr
    "deviceid"
};

static int _aml_unifykey_read(const char* name, char* data, size_t len) {
    int fd = -1;
    struct key_item_info_t item;

    if (!name || !data || len <= 0) {
        return -1;
    }
    fd = open(UNIFYKEYS_CDEV, O_RDWR);
    if (fd < 0) return -1;
    memset((void*)&item, 0, sizeof(item));
    strncpy(item.name, name, sizeof(item.name));
    if (ioctl(fd, KEYUNIFY_ATTACH, &item) != 0) {
        close(fd);
        return -1;
    }

    ioctl(fd, KEYUNIFY_GET_INFO, &item);

    // printf("field:%s -> id:%d -> size:%d\n", name, item.id, item.size);

    if (item.size > len) {
        close(fd);
        return -ENOMEM;
    }
    lseek(fd, item.id, SEEK_SET);

    ssize_t s = read(fd, data, item.size);
    // printf("read: %ld bytes\n", s);
    // printf("read data:%s\n", data);

    close(fd);
    return s;
}

int dm_impl_system_property_get(PROPERTY_e which, char* data, int len) {
    if (!data || which >= _PROPERTY_MAX) {
        return -1;
    }
    return _aml_unifykey_read(_prop_tbl[which], data, len);
}
