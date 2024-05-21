#include <stddef.h>
#include <stdint.h>

#include "j2sobject.h"
#include "j2sresponse.h"



#define _J2SOBJECT_ENCRYPT_FIELDS_OFFSET(ele) \
    offsetof(struct j2sobject_encrypt_resp, ele)

#define _J2SOBJECT_ENCRYPT_FIELDS_LEN(ele) \
    sizeof(((struct j2sobject_encrypt_resp *)0)->ele)

#define _J2SOBJECT_ENCRYPT_DATA_FIELDS_OFFSET(ele) \
    offsetof(struct j2sobject_encrypt_resp_data, ele)

#define _J2SOBJECT_ENCRYPT_DATA_FIELDS_LEN(ele) \
    sizeof(((struct j2sobject_encrypt_resp_data *)0)->ele)


static int j2sobject_encrypt_resp_ctor(struct j2sobject *obj);
static int j2sobject_encrypt_resp_data_ctor(struct j2sobject *obj);

struct j2sobject_prototype _j2sobject_encrypt_resp_prototype = {
    .name = "encrypt_resp",
    .type = J2S_OBJECT,
    .size = sizeof(struct j2sobject_encrypt_resp),
    .ctor = j2sobject_encrypt_resp_ctor,
    .dtor = NULL};


static struct j2sobject_prototype _j2sobject_encrypt_resp_data_prototype = {
    .name = "encrypt_resp_data",
    .type = J2S_OBJECT,
    .size = sizeof(struct j2sobject_encrypt_resp_data),
    .ctor = j2sobject_encrypt_resp_data_ctor,
    .dtor = NULL};


static struct j2sobject_fields_prototype _j2sobject_encrypt_resp_fields_prototype[] = {
    {.name = "result", .type = J2S_INT, .offset = _J2SOBJECT_ENCRYPT_FIELDS_OFFSET(result), .offset_len = 0},
    {.name = "data", .type = J2S_OBJECT, .offset = _J2SOBJECT_ENCRYPT_FIELDS_OFFSET(data), .offset_len = _J2SOBJECT_ENCRYPT_FIELDS_LEN(data), .proto = &_j2sobject_encrypt_resp_data_prototype},
    {0}};

static struct j2sobject_fields_prototype _j2sobject_encrypt_resp_data_fields_prototype[] = {
    {.name = "upSmsOn", .type = J2S_STRING, .offset = _J2SOBJECT_ENCRYPT_DATA_FIELDS_OFFSET(upSmsOn), .offset_len = 0},
    {.name = "pre", .type = J2S_STRING, .offset = _J2SOBJECT_ENCRYPT_DATA_FIELDS_OFFSET(pre), .offset_len = 0},
    {.name = "preDomain", .type = J2S_STRING, .offset = _J2SOBJECT_ENCRYPT_DATA_FIELDS_OFFSET(preDomain), .offset_len = 0},
    {.name = "pubKey", .type = J2S_STRING, .offset = _J2SOBJECT_ENCRYPT_DATA_FIELDS_OFFSET(pubKey), .offset_len = 0},
    {0}};


static int j2sobject_encrypt_resp_ctor(struct j2sobject *obj) {
    if (!obj)
        return -1;
    obj->name = "encrypt_resp";
    obj->field_protos = _j2sobject_encrypt_resp_fields_prototype;
    return 0;
}
static int j2sobject_encrypt_resp_data_ctor(struct j2sobject *obj) {
    if (!obj)
        return -1;
    obj->name = "encrypt_resp_data";
    obj->field_protos = _j2sobject_encrypt_resp_data_fields_prototype;
    return 0;
}
