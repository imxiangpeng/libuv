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


#define _J2SOBJECT_CLOUD_FOLDER_FIELDS_OFFSET(ele) \
    offsetof(struct j2sobject_cloud_folder, ele)


#define _J2SOBJECT_CLOUD_FILE_FIELDS_OFFSET(ele) \
    offsetof(struct j2sobject_cloud_file, ele)

#define _J2SOBJECT_CLOUD_FILE_FIELDS_LEN(ele) \
    sizeof(((struct j2sobject_cloud_file*)0)->ele)

#define _J2SOBJECT_CLOUD_ICON_FIELDS_OFFSET(ele) \
    offsetof(struct j2sobject_cloud_icon, ele)

#define _J2SOBJECT_CLOUD_FOLDER_RESP_FIELDS_OFFSET(ele) \
    offsetof(struct j2sobject_cloud_folder_resp, ele)

static int j2sobject_cloud_folder_ctor(struct j2sobject *obj);
static int j2sobject_cloud_file_ctor(struct j2sobject *obj);
static int j2sobject_cloud_icon_ctor(struct j2sobject *obj);
static int j2sobject_cloud_folder_resp_ctor(struct j2sobject *obj);

struct j2sobject_prototype _j2sobject_could_folder_prototype = {
    .name = "cloud_folder",
    .type = J2S_OBJECT,
    .size = sizeof(struct j2sobject_cloud_folder),
    .ctor = j2sobject_cloud_folder_ctor,
    .dtor = NULL};


static struct j2sobject_fields_prototype _j2sobject_cloud_folder_fields_prototype[] = {
    {.name = "id", .type = J2S_DOUBLE, .offset = _J2SOBJECT_CLOUD_FOLDER_FIELDS_OFFSET(id), .offset_len = 0},
    {.name = "name", .type = J2S_STRING, .offset = _J2SOBJECT_CLOUD_FOLDER_FIELDS_OFFSET(name), .offset_len = 0},
    {.name = "createDate", .type = J2S_STRING, .offset = _J2SOBJECT_CLOUD_FOLDER_FIELDS_OFFSET(createDate), .offset_len = 0},
    {.name = "lastOpTime", .type = J2S_STRING, .offset = _J2SOBJECT_CLOUD_FOLDER_FIELDS_OFFSET(lastOpTime), .offset_len = 0},
    // {.name = "rev", .type = J2S_DOUBLE, .offset = _J2SOBJECT_CLOUD_FOLDER_FIELDS_OFFSET(rev), .offset_len = 0},
    {.name = "rev", .type = J2S_STRING, .offset = _J2SOBJECT_CLOUD_FOLDER_FIELDS_OFFSET(rev), .offset_len = 0},
    {.name = "parentId", .type = J2S_DOUBLE, .offset = _J2SOBJECT_CLOUD_FOLDER_FIELDS_OFFSET(parentId), .offset_len = 1},
    {0}};

static int j2sobject_cloud_folder_ctor(struct j2sobject *obj) {
    if (!obj)
        return -1;
    obj->name = "cloud_folder";
    obj->field_protos = _j2sobject_cloud_folder_fields_prototype;
    return 0;
}

// icon

struct j2sobject_prototype _j2sobject_could_icon_prototype = {
    .name = "cloud_file",
    .type = J2S_OBJECT,
    .size = sizeof(struct j2sobject_cloud_icon),
    .ctor = j2sobject_cloud_icon_ctor,
    .dtor = NULL};

static struct j2sobject_fields_prototype _j2sobject_cloud_icon_fields_prototype[] = {
    {.name = "smallUrl", .type = J2S_STRING, .offset = _J2SOBJECT_CLOUD_ICON_FIELDS_OFFSET(smallUrl), .offset_len = 0},
    {.name = "mediumUrl", .type = J2S_STRING, .offset = _J2SOBJECT_CLOUD_ICON_FIELDS_OFFSET(mediumUrl), .offset_len = 0},
    {.name = "largeUrl", .type = J2S_STRING, .offset = _J2SOBJECT_CLOUD_ICON_FIELDS_OFFSET(largeUrl), .offset_len = 0},
    {.name = "max600", .type = J2S_STRING, .offset = _J2SOBJECT_CLOUD_ICON_FIELDS_OFFSET(max600), .offset_len = 0},
    {0}};


static int j2sobject_cloud_icon_ctor(struct j2sobject *obj) {
    if (!obj)
        return -1;
    obj->name = "cloud_icon";
    obj->field_protos = _j2sobject_cloud_icon_fields_prototype;
    return 0;
}

// file
struct j2sobject_prototype _j2sobject_could_file_prototype = {
    .name = "cloud_file",
    .type = J2S_OBJECT,
    .size = sizeof(struct j2sobject_cloud_file),
    .ctor = j2sobject_cloud_file_ctor,
    .dtor = NULL};

static struct j2sobject_fields_prototype _j2sobject_cloud_file_fields_prototype[] = {
    {.name = "id", .type = J2S_DOUBLE, .offset = _J2SOBJECT_CLOUD_FILE_FIELDS_OFFSET(id), .offset_len = 0},
    {.name = "name", .type = J2S_STRING, .offset = _J2SOBJECT_CLOUD_FILE_FIELDS_OFFSET(name), .offset_len = 0},
    {.name = "createDate", .type = J2S_STRING, .offset = _J2SOBJECT_CLOUD_FILE_FIELDS_OFFSET(createDate), .offset_len = 0},
    {.name = "lastOpTime", .type = J2S_STRING, .offset = _J2SOBJECT_CLOUD_FILE_FIELDS_OFFSET(lastOpTime), .offset_len = 0},
    // {.name = "rev", .type = J2S_DOUBLE, .offset = _J2SOBJECT_CLOUD_FILE_FIELDS_OFFSET(rev), .offset_len = 0},
    {.name = "rev", .type = J2S_STRING, .offset = _J2SOBJECT_CLOUD_FILE_FIELDS_OFFSET(rev), .offset_len = 0},
    {.name = "parentId", .type = J2S_DOUBLE, .offset = _J2SOBJECT_CLOUD_FILE_FIELDS_OFFSET(parentId), .offset_len = 0},
    {.name = "size", .type = J2S_DOUBLE, .offset = _J2SOBJECT_CLOUD_FILE_FIELDS_OFFSET(size), .offset_len = 0},
    {.name = "md5", .type = J2S_STRING, .offset = _J2SOBJECT_CLOUD_FILE_FIELDS_OFFSET(md5), .offset_len = _J2SOBJECT_CLOUD_FILE_FIELDS_LEN(md5)},
    {.name = "mediaType", .type = J2S_INT, .offset = _J2SOBJECT_CLOUD_FILE_FIELDS_OFFSET(mediaType), .offset_len = 0},
    {.name = "orientation", .type = J2S_INT, .offset = _J2SOBJECT_CLOUD_FILE_FIELDS_OFFSET(orientation), .offset_len = 0},
    {.name = "icon", .type = J2S_OBJECT, .offset = _J2SOBJECT_CLOUD_FILE_FIELDS_OFFSET(icon), .offset_len = _J2SOBJECT_CLOUD_FILE_FIELDS_LEN(icon), .proto = &_j2sobject_could_icon_prototype},
    {0}};


static int j2sobject_cloud_file_ctor(struct j2sobject *obj) {
    if (!obj)
        return -1;
    obj->name = "cloud_file";
    obj->field_protos = _j2sobject_cloud_file_fields_prototype;
    return 0;
}

// folder response's fileListAO
struct j2sobject_prototype _j2sobject_could_folder_resp_prototype = {
    .name = "cloud_folder_resp",
    .type = J2S_OBJECT,
    .size = sizeof(struct j2sobject_cloud_folder_resp),
    .ctor = j2sobject_cloud_folder_resp_ctor,
    .dtor = NULL};

static struct j2sobject_fields_prototype _j2sobject_cloud_folder_resp_fields_prototype[] = {
    {.name = "count", .type = J2S_INT, .offset = _J2SOBJECT_CLOUD_FOLDER_RESP_FIELDS_OFFSET(count), .offset_len = 0},
    {.name = "fileListSize", .type = J2S_INT, .offset = _J2SOBJECT_CLOUD_FOLDER_RESP_FIELDS_OFFSET(fileListSize), .offset_len = 0},
    {.name = "folderList", .type = J2S_ARRAY, .offset = _J2SOBJECT_CLOUD_FOLDER_RESP_FIELDS_OFFSET(folderList), .offset_len = 0, .proto = &_j2sobject_could_folder_prototype},
    {.name = "fileList", .type = J2S_ARRAY, .offset = _J2SOBJECT_CLOUD_FOLDER_RESP_FIELDS_OFFSET(fileList), .offset_len = 0, .proto = &_j2sobject_could_file_prototype},
    {.name = "lastRev", .type = J2S_DOUBLE, .offset = _J2SOBJECT_CLOUD_FOLDER_RESP_FIELDS_OFFSET(lastRev), .offset_len = 0},
    {0}};


static int j2sobject_cloud_folder_resp_ctor(struct j2sobject *obj) {
    if (!obj)
        return -1;
    obj->name = "cloud_file";
    obj->field_protos = _j2sobject_cloud_folder_resp_fields_prototype;
    return 0;
}