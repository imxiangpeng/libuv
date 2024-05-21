

#include "j2sobject.h"

// {"result":0,"data":{"upSmsOn":"0","pre":"{NRP}","preDomain":"card.e.189.cn","pubKey":"MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQCZLyV4gHNDUGJMZoOcYauxmNEsKrc0TlLeBEVVIIQNzG4WqjimceOj5R9ETwDeeSN3yejAKLGHgx83lyy2wBjvnbfm/nLObyWwQD/09CmpZdxoFYCH6rdDjRpwZOZ2nXSZpgkZXoOBkfNXNxnN74aXtho2dqBynTw3NFTWyQl8BQIDAQAB"}}
typedef struct j2sobject_encrypt_resp_data {
    J2SOBJECT_DECLARE_OBJECT;
    char *upSmsOn;
    char *pre;
    char *preDomain;
    char *pubKey;
} j2sobject_encrypt_resp_data_t;

typedef struct j2sobject_encrypt_resp {
    J2SOBJECT_DECLARE_OBJECT;
    int result;
    j2sobject_encrypt_resp_data_t data;
} j2sobject_encrypt_resp_t;

typedef struct j2sobject_cloud_folder {
    J2SOBJECT_DECLARE_OBJECT;
    double id; // long
    char *name;
    char *createDate;
    char *lastOpTime;
    // double rev; // long
    char *rev; // long
    double parentId; // long

} j2sobject_cloud_folder_t;

typedef struct j2sobject_cloud_icon {
    J2SOBJECT_DECLARE_OBJECT;
    char *smallUrl;
    char *mediumUrl;
    char *largeUrl;
    char *max600;
} j2sobject_cloud_icon_t;

typedef struct j2sobject_cloud_file {
    J2SOBJECT_DECLARE_OBJECT;
    double id; // long
    char *name;
    char *createDate;
    char *lastOpTime;
    // double rev; // long 
    char *rev; // long 
    double parentId; // long
    double size; // long
    char md5[32 + 4];
    int mediaType;
    int orientation;
    // ignore media attr list
    // mediaAttrList*;

    // icon
    j2sobject_cloud_icon_t icon;

} j2sobject_cloud_file_t;


// fileListAO
typedef struct j2sobject_cloud_folder_resp {
    J2SOBJECT_DECLARE_OBJECT;
    int count;
    int fileListSize;
    j2sobject_cloud_folder_t *folderList;
    j2sobject_cloud_file_t *fileList;
    double lastRev;
} j2sobject_cloud_folder_resp_t;

extern struct j2sobject_prototype _j2sobject_encrypt_resp_prototype;
extern struct j2sobject_prototype _j2sobject_cloud_folder_prototype;
extern struct j2sobject_prototype _j2sobject_cloud_file_prototype;

//#define J2SOBJECT_DEL(self) \
//    j2sobject_free(J2SOBJECT(self))

#define J2SENCRYPT_RESP_NEW() \
    (j2sobject_encrypt_resp_t *)j2sobject_create(&_j2sobject_encrypt_resp_prototype)
