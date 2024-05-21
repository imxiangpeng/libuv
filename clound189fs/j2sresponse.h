

#include "j2sobject.h"


// {"result":0,"data":{"upSmsOn":"0","pre":"{NRP}","preDomain":"card.e.189.cn","pubKey":"MIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQCZLyV4gHNDUGJMZoOcYauxmNEsKrc0TlLeBEVVIIQNzG4WqjimceOj5R9ETwDeeSN3yejAKLGHgx83lyy2wBjvnbfm/nLObyWwQD/09CmpZdxoFYCH6rdDjRpwZOZ2nXSZpgkZXoOBkfNXNxnN74aXtho2dqBynTw3NFTWyQl8BQIDAQAB"}}
typedef struct j2sobject_encrypt_resp_data {
    J2SOBJECT_DECLARE_OBJECT;
    char *upSmsOn;
    char *pre;
    char *preDomain;
    char* pubKey;
} j2sobject_encrypt_resp_data_t;

typedef struct j2sobject_encrypt_resp {
    J2SOBJECT_DECLARE_OBJECT;
    int result;
    j2sobject_encrypt_resp_data_t data;
} j2sobject_encrypt_resp_t;


extern struct j2sobject_prototype _j2sobject_encrypt_resp_prototype;

//#define J2SOBJECT_DEL(self) \
//    j2sobject_free(J2SOBJECT(self))

#define J2SENCRYPT_RESP_NEW() \
    (j2sobject_encrypt_resp_t*)j2sobject_create(&_j2sobject_encrypt_resp_prototype)
