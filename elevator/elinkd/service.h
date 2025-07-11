#ifndef SERVICE_H
#define SERVICE_H
#include <cjson/cJSON.h>

struct svc_action {
    const char* name;
    int (*method)(cJSON* param);
};

extern struct svc_action svc_action_tbl[];
extern size_t svc_action_tbl_size;
#endif