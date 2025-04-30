#include "platform.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/netlink.h>
#include <linux/rtnetlink.h>
#include <mosquitto.h>
#include <net/if.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

#include "hr_log.h"

#define BUFLEN 8192

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
    "usid",  // serial
    "mac",   // macaddr
    "deviceid"};

static int _aml_unifykey_read(const char* name, char* data, size_t len) {
    int fd = -1;
    struct key_item_info_t item;

    if (!name || !data || len <= 0) {
        return -1;
    }
    fd = open(UNIFYKEYS_CDEV, O_RDWR);
    if (fd < 0)
        return -1;
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

static int get_default_iface_netlink(char* iface, size_t size) {
    (void)size;
    int sock = socket(AF_NETLINK, SOCK_RAW, NETLINK_ROUTE);
    if (sock < 0) {
        perror("socket");
        return -1;
    }

    struct {
        struct nlmsghdr nlh;
        struct rtmsg rt;
    } req;

    memset(&req, 0, sizeof(req));
    req.nlh.nlmsg_len = NLMSG_LENGTH(sizeof(struct rtmsg));
    req.nlh.nlmsg_type = RTM_GETROUTE;
    req.nlh.nlmsg_flags = NLM_F_REQUEST | NLM_F_DUMP;
    req.rt.rtm_family = AF_INET;

    struct sockaddr_nl sa = {.nl_family = AF_NETLINK};

    if (sendto(sock, &req, req.nlh.nlmsg_len, 0,
               (struct sockaddr*)&sa, sizeof(sa)) < 0) {
        perror("sendto");
        close(sock);
        return -1;
    }

    char buf[BUFLEN];
    ssize_t len;
    while ((len = recv(sock, buf, sizeof(buf), 0)) > 0) {
        struct nlmsghdr* nlh;
        for (nlh = (struct nlmsghdr*)buf;
             NLMSG_OK(nlh, len);
             nlh = NLMSG_NEXT(nlh, len)) {
            if (nlh->nlmsg_type == NLMSG_DONE)
                break;

            if (nlh->nlmsg_type == NLMSG_ERROR) {
                fprintf(stderr, "Netlink error\n");
                close(sock);
                return -1;
            }

            struct rtmsg* rt = NLMSG_DATA(nlh);
            if (rt->rtm_family != AF_INET || rt->rtm_table != RT_TABLE_MAIN)
                continue;

            struct rtattr* attr = RTM_RTA(rt);
            int attr_len = RTM_PAYLOAD(nlh);

            int ifindex = -1;
            uint32_t dst = 0;

            for (; RTA_OK(attr, attr_len); attr = RTA_NEXT(attr, attr_len)) {
                if (attr->rta_type == RTA_OIF) {
                    ifindex = *(int*)RTA_DATA(attr);
                } else if (attr->rta_type == RTA_DST) {
                    dst = *(uint32_t*)RTA_DATA(attr);
                }
            }

            // Default route: no destination
            if (dst == 0 && ifindex > 0) {
                if (!if_indextoname(ifindex, iface)) {
                    perror("if_indextoname");
                    close(sock);
                    return -1;
                }
                close(sock);
                return 0;
            }
        }
    }

    close(sock);
    return -1;
}
int platform_get_property(PROPERTY_e which, char* data, int len) {
    if (!data || which >= _PROPERTY_MAX) {
        return -1;
    }
    return _aml_unifykey_read(_prop_tbl[which], data, len);
}

// use fixed mac address
int platform_get_mac_address(char* mac, size_t size) {
    _aml_unifykey_read(_prop_tbl[PROPERTY_MACADDR], mac, size);
    return 0;
}
// maybe we should use running address
int platform_get_ip_address(char* ip, size_t size) {
    struct ifreq ifr;
    char iface[IFNAMSIZ] = {0};

    if (0 != get_default_iface_netlink(iface, sizeof(iface))) {
        HR_LOGE("Failed to get default interface\n");
    }

    HR_LOGD("Default interface: %s\n", iface);
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        return -1;
    }

    memset(&ifr, 0, sizeof(ifr));
    strncpy(ifr.ifr_name, iface, IFNAMSIZ);

    if (ioctl(fd, SIOCGIFHWADDR, &ifr) < 0) {
        close(fd);
        return -1;
    }

    unsigned char* mac = (unsigned char*)ifr.ifr_hwaddr.sa_data;

    HR_LOGD("mac: %02X%02X%02X%02X%02X%02X", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    if (ioctl(fd, SIOCGIFADDR, &ifr) < 0) {
        close(fd);
        return -1;
    }

    struct sockaddr_in* ipaddr = (struct sockaddr_in*)&ifr.ifr_addr;
    strncpy(ip, inet_ntoa(ipaddr->sin_addr), size);
    HR_LOGD("ip:%s\n", ip);
    return 0;
}

const char* platform_get_const_mac_address(void) {

    return "";
}
const char* platform_get_const_ip_address(void) {
    return "";
}