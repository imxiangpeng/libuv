#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif

#include <ctype.h>
#include <curl/curl.h>
#include <curl/urlapi.h>
#include <errno.h>
#include <fcntl.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <openssl/aes.h>
#include <openssl/err.h>
#include <openssl/hmac.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <uuid/uuid.h>

#include "j2sobject_response.h"
#include "xxtea.h"

#define WEB_BASE_URL "https://cloud.189.cn"
#define APPID "8025431004"
#define CLI_TYPE "10020"
#define RETURN_URL "https://m.cloud.189.cn/zhuanti/2020/loginErrorPc/index.html"

#define PC "TELEPC"
#define MAC "TELEMAC"
struct _memory_buffer {
    char *data;
    size_t size;
    size_t offset;
};

static struct _memory_buffer detect_data = {0};
int memory_buffer_alloc(struct _memory_buffer *buf, size_t size) {
    if (!buf)
        return -1;

    // assert(!buf->data);

    // assert(buf->size > 0);
    buf->offset = 0;
    buf->size = size;
    buf->data = (char *)calloc(1, buf->size);
    if (!buf->data) {
        printf("%s(%d): can not allocate memory ...\n", __FUNCTION__, __LINE__);
        return -1;
    }

    return 0;
}
int memory_buffer_realloc(struct _memory_buffer *buf, size_t size) {
    if (!buf /*|| !buf->data*/)
        return -1;

    buf->data = (char *)realloc(buf->data, size);
    if (!buf->data) {
        printf("%s(%d): can not reallocate memory ...\n", __FUNCTION__, __LINE__);
        return -1;
    }
    buf->size = size;
    memset((void *)((const char *)buf->data + buf->offset), 0, buf->size - buf->offset);
    return 0;
}
int memory_buffer_free(struct _memory_buffer *buf) {
    if (!buf)
        return -1;
    buf->offset = 0;
    memset((void *)buf->data, 0, buf->size);

    free(buf->data);

    memset((void *)buf, 0, sizeof(*buf));
    return 0;
}
int memory_buffer_reset(struct _memory_buffer *buf) {
    if (!buf || !buf->data)
        return -1;
    buf->offset = 0;
    memset((void *)buf->data, 0, buf->size);

    return 0;
}

int memory_buffer_append(struct _memory_buffer *buf, void *data, size_t size) {
    if (!buf || !buf->data || !data || size == 0)
        return -1;
    if (size > buf->size - buf->offset) {
        int ret = memory_buffer_realloc(buf, buf->size + size);
        if (ret != 0) {
            return -1;  // memory not enough
        }
    }
    memcpy((void *)(buf->data + buf->offset), data, size);
    buf->offset += size;
    return 0;
}

static size_t _data_receive(void *ptr, size_t size, size_t nmemb,
                            void *userdata) {
    struct _memory_buffer *buf = (struct _memory_buffer *)userdata;

    size_t total = size * nmemb;

    if (!ptr || !userdata)
        return -1;
#if 0
    if (buf->size - buf->offset < total) {
        memory_buffer_realloc(buf, buf->size + total + 1);
    }

    memcpy((void *)(buf->data + buf->offset), ptr, total);
    buf->offset += total;
#endif
    memory_buffer_append(buf, ptr, total);
    return total;
}
#if 0
static int _update_post_data(const char *url, const char *data) {
    long response_code = 0;
    CURL *curl = NULL;

    curl = curl_easy_init();
    if (!curl) return -1;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, strlen(data));

    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_SERVER_RESPONSE_TIMEOUT, 60L);

    // drop all response data
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, _data_receive_dummy);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, NULL);

    CURLcode code = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    curl_easy_cleanup(curl);

    if (CURLE_OK != code) {
        printf("request failed ... %d:%s\n", code, curl_easy_strerror(code));
        return -1;
    }

    if (response_code != 200) return -1;

    return 0;
}
#endif
int http_post(const char *url, struct curl_slist *headers, const char *payload, size_t payload_length, struct _memory_buffer *result) {
    CURL *curl;
    CURLcode res;
    curl_mime *form = NULL;
    curl_mimepart *field = NULL;

    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();
    if (curl) {
        // curl_easy_setopt(curl, CURLOPT_URL, /*AUTH_URL*/ "http://10.30.11.78/api/logbox/oauth2/loginSubmit.do");
        curl_easy_setopt(curl, CURLOPT_URL, url);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, _data_receive);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, result);

        // follow redirect
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);

        // char postFields[2048];
        // snprintf(postFields, sizeof(postFields),
        //          "appKey=%s&accountType=%s&userName=%s&password=%s&validateCode=%s&captchaToken=%s&returnUrl=%s"
        //          "&dynamicCheck=FALSE&clientType=%s&cb_SaveName=1&isOauth2=false&state=&paramId=%s",
        //          APP_ID, ACCOUNT_TYPE, rsaUsername, rsaPassword, vcode, captchaToken, RETURN_URL, CLIENT_TYPE, paramId);

        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, payload_length);
        res = curl_easy_perform(curl);

        if (res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        } else {
            printf("%lu bytes retrieved\n", (unsigned long)result->offset);
            printf("Response: %s\n", result->data);
        }

        curl_mime_free(form);
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
    }
    curl_global_cleanup();

    return 0;
}

int http_post_j2sobject_result(const char *url, struct curl_slist *headers, const char *payload, size_t payload_length, struct j2sobject *result) {
    struct _memory_buffer buffer;

    memory_buffer_alloc(&buffer, 2048);

    int ret = http_post(url, headers, payload, payload_length, &buffer);
    printf("ret:%d\n", ret);
    if (ret == 0) {
        printf("response json data:%s\n", buffer.data);
        ret = j2sobject_deserialize(result, buffer.data);
        printf("ret:%d\n", ret);
    }
    memory_buffer_free(&buffer);

    return ret;
}
int driver_http_get(const char *url, struct curl_slist *headers, const char *payload, size_t payload_length, struct _memory_buffer *result) {
    CURL *curl;
    CURLcode res;

    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();
    if (!curl) {
        return -1;
    }
    // curl_easy_setopt(curl, CURLOPT_URL, /*AUTH_URL*/ "http://10.30.11.78/api/logbox/oauth2/loginSubmit.do");
    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, _data_receive);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, result);

    // follow redirect
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);

    res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        return -1;
    }
    printf("%lu bytes retrieved\n", (unsigned long)result->offset);
    printf("Response: %s\n", result->data);

    curl_easy_cleanup(curl);
    curl_global_cleanup();

    return 0;
}

static int http_get(CURLU *url, /*struct curl_slist *data, struct curl_slist *header, */ struct _memory_buffer *mem) {
    long response_code = 0;
    CURL *curl = NULL;

    if (!url) return -1;

    curl = curl_easy_init();
    if (!curl) return -1;

    char *uri = NULL;
    curl_url_get(url, CURLUPART_URL, &uri, 0);
    printf("request: %s\n", uri);
    curl_easy_setopt(curl, CURLOPT_CURLU, url);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    // curl_easy_setopt(curl, CURLOPT_CAINFO, "/data/cacert.pem");
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_SERVER_RESPONSE_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, _data_receive);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, mem);

    // follow redirect
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);

    CURLcode code = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    curl_easy_cleanup(curl);
    printf("code:%d, response code:%ld\n", CURLE_OK, response_code);

    if (CURLE_OK != code) {
        printf("request failed ... %d:%s\n", code, curl_easy_strerror(code));
        return -1;
    }

    if (response_code != 200) return -1;

    return 0;
}

static long long time_ms() {
    struct timespec ts;
    if (clock_gettime(CLOCK_REALTIME, &ts) == -1) {
        perror("clock_gettime");
        return -1;
    }
    return (ts.tv_sec * 1000LL + ts.tv_nsec / 1000000LL);
}

// https://cloud.189.cn/api/portal/unifyLoginForPC.action?appId=8025431004&clientType=10020&returnURL=https%3A%2F%2Fm.cloud.189.cn%2Fzhuanti%2F2020%2FloginErrorPc%2Findex.html&timeStamp=1716172904767

//    var lt = "A7BEF6A83B0C545FB6A6DC336C4AEA255550313A9987219D29AA22886F20597A7B0F3B6C142C96595D6EA3FD141C067F2C230618EA06E5C4A2A059721318FD0BA79F12D52C615D87097DD8738E83DDA4C5DC2E67E2153488";
//    var paramId = "F6E2D133B1DE795D7FE4B43F902B1F660E18485E9EBDC770E32A05AFE6725CB935B3779A59C6351EC8A50BD4";
//    var reqId = "309b33c1e2f141dd";
//    var guid = "62680f20cb1444e2adb3fef38bcf51b6";
static char *extract_value(const char *source, const char *pattern) {
    regex_t regex;
    regmatch_t matches[2];
    char *value = NULL;

    if (regcomp(&regex, pattern, REG_EXTENDED) != 0) {
        fprintf(stderr, "Could not compile regex\n");
        return NULL;
    }

    if (regexec(&regex, source, 2, matches, 0) == 0) {
        int start = matches[1].rm_so;
        int end = matches[1].rm_eo;
        int length = end - start;

        value = (char *)malloc(length + 1);
        strncpy(value, source + start, length);
        value[length] = '\0';
    }

    regfree(&regex);
    return value;
}
int request_login() {
    // char* url = NULL;
    struct curl_slist *query_data = NULL;

    char tmp[256] = {0};
    CURLU *url = curl_url();

    memory_buffer_alloc(&detect_data, 2048);
    int rc = curl_url_set(url, CURLUPART_URL, WEB_BASE_URL "/api/portal/unifyLoginForPC.action", 0);
    curl_url_set(url, CURLUPART_QUERY, "appId=" APPID, CURLU_APPENDQUERY);
    curl_url_set(url, CURLUPART_QUERY, "clientType=" CLI_TYPE, CURLU_APPENDQUERY);
    curl_url_set(url, CURLUPART_QUERY, "returnURL=" RETURN_URL, CURLU_APPENDQUERY);
    snprintf(tmp, sizeof(tmp), "timeStamp=%lld", time_ms());
    curl_url_set(url, CURLUPART_QUERY, tmp, CURLU_APPENDQUERY);

    http_get(url, &detect_data);

    printf("response data:%s\n", detect_data.data);

    char *captchaToken = extract_value(detect_data.data, "name='captchaToken' value='([^']+)'");
    char *lt = extract_value(detect_data.data, "var lt = \"([^\"]+)\"");
    char *paramId = extract_value(detect_data.data, "var paramId = \"([^\"]+)\"");
    char *reqId = extract_value(detect_data.data, "var reqId = \"([^\"]+)\"");
    char *jRsaKey = extract_value(detect_data.data, "id=\"j_rsaKey\" value=\"([^\"]+)\"");

    // Print extracted parameters
    printf("CaptchaToken: %s\n", captchaToken);
    printf("Lt: %s\n", lt);
    printf("ParamId: %s\n", paramId);
    printf("ReqId: %s\n", reqId);
    printf("jRsaKey: %s\n", jRsaKey);

    free(captchaToken);
    free(lt);
    free(paramId);
    free(reqId);
    free(jRsaKey);
    return 0;
}
#if 0
static char* RsaEncrypt(const char *publicKey, const char *origData) {
    BIO *bio = BIO_new_mem_buf((void*)publicKey, -1);
    RSA *rsa = PEM_read_bio_RSA_PUBKEY(bio, NULL, NULL, NULL);

    if (rsa == NULL) {
        ERR_print_errors_fp(stderr);
        return NULL;
    }

    int rsa_len = RSA_size(rsa);
    unsigned char *encrypted = (unsigned char*)malloc(rsa_len);
    int result = RSA_public_encrypt(strlen(origData), (unsigned char*)origData, encrypted, rsa, RSA_PKCS1_PADDING);

    if (result == -1) {
        ERR_print_errors_fp(stderr);
        return NULL;
    }

    char *hex = (char*)malloc(rsa_len * 2 + 1);
    for (int i = 0; i < rsa_len; i++) {
        sprintf(hex + i * 2, "%02X", encrypted[i]);
    }

    free(encrypted);
    RSA_free(rsa);
    BIO_free_all(bio);
    return hex;
}
#endif

char *RsaEncrypt(const char *publicKey, const char *origData) {
    BIO *bio = BIO_new_mem_buf((void *)publicKey, -1);
    EVP_PKEY *evp_pkey = NULL;
    EVP_PKEY_CTX *ctx = NULL;
    size_t outlen = 0;
    unsigned char *outbuf = NULL;
    char *hex = NULL;

    if (!bio) {
        fprintf(stderr, "Error creating BIO\n");
        return NULL;
    }

    evp_pkey = PEM_read_bio_PUBKEY(bio, NULL, NULL, NULL);
    BIO_free(bio);

    if (!evp_pkey) {
        fprintf(stderr, "Error loading public key\n");
        return NULL;
    }
    ctx = EVP_PKEY_CTX_new(evp_pkey, NULL);
    if (!ctx) {
        fprintf(stderr, "Error creating EVP_PKEY_CTX\n");
        EVP_PKEY_free(evp_pkey);
        return NULL;
    }
    if (EVP_PKEY_encrypt_init(ctx) <= 0) {
        fprintf(stderr, "Error initializing encryption context\n");
        EVP_PKEY_CTX_free(ctx);
        EVP_PKEY_free(evp_pkey);
        return NULL;
    }

    if (EVP_PKEY_CTX_set_rsa_padding(ctx, RSA_PKCS1_PADDING) <= 0) {
        fprintf(stderr, "Error setting padding\n");
        EVP_PKEY_CTX_free(ctx);
        EVP_PKEY_free(evp_pkey);
        return NULL;
    }

    if (EVP_PKEY_encrypt(ctx, NULL, &outlen, (const unsigned char *)origData, strlen(origData)) <= 0) {
        fprintf(stderr, "Error determining buffer length\n");
        EVP_PKEY_CTX_free(ctx);
        EVP_PKEY_free(evp_pkey);
        return NULL;
    }

    outbuf = (unsigned char *)malloc(outlen);
    if (!outbuf) {
        fprintf(stderr, "Error allocating memory for encrypted data\n");
        EVP_PKEY_CTX_free(ctx);
        EVP_PKEY_free(evp_pkey);
        return NULL;
    }

    memset((void *)outbuf, 0, outlen);
    if (EVP_PKEY_encrypt(ctx, outbuf, &outlen, (const unsigned char *)origData, strlen(origData)) <= 0) {
        fprintf(stderr, "Error encrypting data\n");
        free(outbuf);
        EVP_PKEY_CTX_free(ctx);
        EVP_PKEY_free(evp_pkey);
        return NULL;
    }

    hex = (char *)malloc(outlen * 2 + 1);
    if (!hex) {
        fprintf(stderr, "Error allocating memory for hex output\n");
        free(outbuf);
        EVP_PKEY_CTX_free(ctx);
        EVP_PKEY_free(evp_pkey);
        return NULL;
    }

    memset((void *)hex, 0, outlen * 2 + 1);

    for (size_t i = 0; i < outlen; i++) {
        sprintf(hex + i * 2, "%02X", outbuf[i]);
    }

    free(outbuf);
    EVP_PKEY_CTX_free(ctx);
    EVP_PKEY_free(evp_pkey);
    return hex;
}

#define ACCOUNT_TYPE "02"
#define APP_ID "8025431004"
#define CLIENT_TYPE "10020"
#define VERSION "6.2"
#define CHANNEL_ID "web_cloud.189.cn"

#define AUTH_URL "https://open.e.189.cn"
#define API_URL "https://api.cloud.189.cn"
#define UPLOAD_URL "https://upload.cloud.189.cn"

//	param := y.loginParam
//	var loginresp LoginResp
//	_, err = y.client.R().
//		ForceContentType("application/json;charset=UTF-8").SetResult(&loginresp).
//		SetHeaders(map[string]string{
//			"REQID": param.ReqId,
//			"lt":    param.Lt,
//		}).
//		SetFormData(map[string]string{
//			"appKey":       APP_ID,
//			"accountType":  ACCOUNT_TYPE,
//			"userName":     param.RsaUsername,
//			"password":     param.RsaPassword,
//			"validateCode": y.VCode,
//			"captchaToken": param.CaptchaToken,
//			"returnUrl":    RETURN_URL,
//			// "mailSuffix":   "@189.cn",
//			"dynamicCheck": "FALSE",
//			"clientType":   CLIENT_TYPE,
//			"cb_SaveName":  "1",
//			"isOauth2":     "false",
//			"state":        "",
//			"paramId":      param.ParamId,
//		}).
//		Post(AUTH_URL + "/api/logbox/oauth2/loginSubmit.do"

// accountType:02
// appKey:8025431004
// captchaToken:8b9040e70f025569bccfc68b5efdc461lwepravo
// cb_SaveName:1 clientType:10020
// dynamicCheck:FALSE
// isOauth2:false
// paramId:1D062D4A33550D3C043FD915105FF3D0685F5C4D7ED89A2976DF6624B05853091A99594999EC386BF5B5F079
// password:{NRP}731D454213635B77D0FEB2F6E080484398DD8C77C8EBDA8634B3E0E92C42175EAA5D6CEB7BA9966D54291919E777776CE9A834F6139FFEC39486FCD5485EA34204E74274A720B056A7B05B752D5BD2CDB1F9D228599CAE8235B9FDC0F5BEFC2859E59E1DEE5ED1E3CB82A5BA9317CF9F5EFE3C5830EA0F869377F8F546A55BC6 returnUrl:https://m.cloud.189.cn/zhuanti/2020/loginErrorPc/index.html
// state:
// userName:{NRP}622E6CDEF287E7B4A3FA78861B8144256C38949A526DB2C7540AECE755CC0384B82DE7E063D1F6648D0D8002FDC33CCDD811586A03B0AFE214CCBD6E9C9D8D12165B8177BBCA6F0781272405340600B898CEA74135744EEFA0DB58AF384BA8A14DA56570D78A16C2116E3121D37CC736FD179A6BD3C8177D6A491D3FE02A5781 validateCode:]
void performLogin(const char *reqId, const char *lt, const char *rsaUsername, const char *rsaPassword, const char *captchaToken, const char *vcode, const char *paramId) {
    CURL *curl;
    CURLcode res;
    curl_mime *form = NULL;
    curl_mimepart *field = NULL;

    curl_global_init(CURL_GLOBAL_ALL);
    curl = curl_easy_init();
    if (curl) {
        struct curl_slist *headers = NULL;
        // headers = curl_slist_append(headers, "Content-Type: application/json;charset=UTF-8");

        char reqIdHeader[256];
        snprintf(reqIdHeader, sizeof(reqIdHeader), "REQID: %s", reqId);
        headers = curl_slist_append(headers, reqIdHeader);

        char ltHeader[256];
        snprintf(ltHeader, sizeof(ltHeader), "lt: %s", lt);
        headers = curl_slist_append(headers, ltHeader);

        headers = curl_slist_append(headers, "Referer: https://cloud.189.cn/");

        // curl_easy_setopt(curl, CURLOPT_URL, /*AUTH_URL*/ "http://10.30.11.78/api/logbox/oauth2/loginSubmit.do");
        curl_easy_setopt(curl, CURLOPT_URL, AUTH_URL "/api/logbox/oauth2/loginSubmit.do");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, _data_receive);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &detect_data);

        memory_buffer_reset(&detect_data);

        // follow redirect
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1);

        char postFields[2048];
        snprintf(postFields, sizeof(postFields),
                 "appKey=%s&accountType=%s&userName=%s&password=%s&validateCode=%s&captchaToken=%s&returnUrl=%s"
                 "&dynamicCheck=FALSE&clientType=%s&cb_SaveName=1&isOauth2=false&state=&paramId=%s",
                 APP_ID, ACCOUNT_TYPE, rsaUsername, rsaPassword, vcode, captchaToken, RETURN_URL, CLIENT_TYPE, paramId);

        char *payload = curl_escape(postFields, 0);

        printf("now payload:%s\n", payload);
        curl_easy_setopt(curl, CURLOPT_POST, 1L);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postFields);

        free(payload);
        // curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, strlen(postFields));
        res = curl_easy_perform(curl);

        if (res != CURLE_OK) {
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
        } else {
            printf("%lu bytes retrieved\n", (unsigned long)detect_data.offset);
            printf("Response: %s\n", detect_data.data);
        }

        curl_mime_free(form);
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
    }
    curl_global_cleanup();
}

//	// 获取Session
//	var erron RespErr
//	var tokenInfo AppSessionResp
//	_, err = y.client.R().
//		SetResult(&tokenInfo).SetError(&erron).
//		SetQueryParams(clientSuffix()).
//		SetQueryParam("redirectURL", url.QueryEscape(loginresp.ToUrl)).
//		Post(API_URL + "/getSessionForPC.action")
//	if err != nil {
//		return
//
// <?xml version="1.0" encoding="UTF-8"?>
// <userSession><loginName>18663792866@189.cn</loginName><sessionKey>7b540d66-5ab2-4e7c-ab66-d10d192c7495</sessionKey><sessionSecret>6E56131239410017348D50FF3D00D425</sessionSecret><keepAlive>1000</keepAlive><getFileDiffSpan>60</getFileDiffSpan><getUserInfoSpan>600</getUserInfoSpan><familySessionKey>96600a3f-1cd1-4a84-b902-d7c72e380ca0_family</familySessionKey><familySessionSecret>6E56131239410017348D50FF3D00D425</familySessionSecret><accessToken>5861c56dd9c944108438cc48b0756a6e</accessToken><refreshToken>8b43bbae19064831a38ef6bc87f53cea</refreshToken><isSaveName>true</isSaveName></userSession>
// <?xml version="1.0" encoding="UTF-8"?>
// <userSession>
// 	<loginName>18663792866@189.cn</loginName>
// 	<sessionKey>7b540d66-5ab2-4e7c-ab66-d10d192c7495</sessionKey>
// 	<sessionSecret>6E56131239410017348D50FF3D00D425</sessionSecret>
// 	<keepAlive>1000</keepAlive>
// 	<getFileDiffSpan>60</getFileDiffSpan>
// 	<getUserInfoSpan>600</getUserInfoSpan>
// 	<familySessionKey>96600a3f-1cd1-4a84-b902-d7c72e380ca0_family</familySessionKey>
// 	<familySessionSecret>6E56131239410017348D50FF3D00D425</familySessionSecret>
// 	<accessToken>5861c56dd9c944108438cc48b0756a6e</accessToken>
// 	<refreshToken>8b43bbae19064831a38ef6bc87f53cea</refreshToken>
// 	<isSaveName>true</isSaveName>
// </userSession>
static xmlNode *firstElement(xmlNode *node, const xmlChar *tag) {
    // internal api must not null
    for (xmlNode *child = node->children; child; child = child->next) {
        // printf("child:%s vs tag:%s\n", child->name, tag);
        if (xmlStrEqual(child->name, tag)) {
            return child;
        }
    }
    return NULL;
}

static xmlNode *nextSibling(xmlNode *node, const xmlChar *tag) {
    // internal api must not null
    while ((node = node->next) != NULL) {
        if (xmlStrEqual(node->name, tag)) {
            return node;
        }
    }
    return NULL;
}
int request_session_token(const char *to_url, char *secret, char *key) {
    struct curl_slist *headers = NULL;
    // headers = curl_slist_append(headers, "Content-Type: application/json;charset=UTF-8");

    char *url = NULL;
    char *payload = "AppId: " APPID;
    struct _memory_buffer buffer;

    memory_buffer_alloc(&buffer, 2048);

    char *redirect = curl_escape(to_url, 0);
    CURLU *uri = curl_url();

    asprintf(&url, API_URL "/getSessionForPC.action?clientType=%s&version=%s&channelId=%s&rand=%d_%d&redirectURL=%s", PC, VERSION, CHANNEL_ID, rand(), rand(), redirect);
    // asprintf(&url,"http://10.30.11.78/getSessionForPC.action?clientType=%s&version=%s&channelId=%s&rand=%d_%d&redirectURL=%s", PC, VERSION, CHANNEL_ID, rand(), rand(), redirect);

    printf("url:%s\n", url);
    int rc = curl_url_set(uri, CURLUPART_URL, url, 0);

    int ret = http_get(uri, &buffer);

    printf("result:%d, config:%s\n", ret, buffer.data);

    xmlDocPtr doc = xmlReadMemory(buffer.data, buffer.offset, "", NULL, 0);
    if (!doc)
        return -1;

    xmlNode *root = xmlDocGetRootElement(doc);
    if (root == NULL) {
        xmlFreeDoc(doc);
        return -1;
    }

    printf("root:%s\n", root->name);

    xmlChar *login_name = xmlNodeGetContent(firstElement(root, "loginName"));
    xmlChar *session_key = xmlNodeGetContent(firstElement(root, "sessionKey"));
    xmlChar *session_secret = xmlNodeGetContent(firstElement(root, "sessionSecret"));
    xmlChar *family_session_key = xmlNodeGetContent(firstElement(root, "familySessionKey"));
    xmlChar *family_session_secret = xmlNodeGetContent(firstElement(root, "familySessionSecret"));
    xmlChar *access_token = xmlNodeGetContent(firstElement(root, "accessToken"));
    xmlChar *refresh_token = xmlNodeGetContent(firstElement(root, "refreshToken"));

    printf("login name:%s\n", login_name);
    printf("session key:%s\n", session_key);
    printf("session secret:%s\n", session_secret);
    printf("family_session_key:%s\n", family_session_key);
    printf("family session secret:%s\n", family_session_secret);
    printf("access token:%s\n", access_token);
    printf("refresh token:%s\n", refresh_token);

    memcpy(secret, session_secret, strlen(session_secret));
    memcpy(key, session_key, strlen(session_key));

    xmlFreeDoc(doc);

    xmlCleanupParser();
    memory_buffer_free(&buffer);
    free(url);
    return 0;
}

// aes ecb encrypt using session secret

#if 0
void PKCS7Padding(unsigned char *data, size_t data_len, size_t block_size) {
    size_t padding_len = block_size - (data_len % block_size);
    for(size_t i = 0; i < padding_len; ++i) {
        data[data_len + i] = (unsigned char)(padding_len);
    }
}
#endif

unsigned char *PKCS7Padding(const unsigned char *data, int data_len, int block_size, int *padded_len) {
    int pad_len = block_size - (data_len % block_size);
    *padded_len = data_len + pad_len;
    unsigned char *padded = (unsigned char *)malloc(*padded_len);
    memcpy(padded, data, data_len);
    memset(padded + data_len, pad_len, pad_len);
    return padded;
}

char *AesECBEncrypt(const char *data, const char *key) {
    EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        return NULL;
    }

    const EVP_CIPHER *cipher = EVP_aes_128_ecb();
    if (!cipher) {
        EVP_CIPHER_CTX_free(ctx);
        return NULL;
    }

    if (EVP_EncryptInit_ex(ctx, cipher, NULL, (const unsigned char *)key, NULL) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return NULL;
    }

    int data_len = strlen(data);
    int block_size = EVP_CIPHER_block_size(cipher);
    int padded_len;
    unsigned char *padded_data = PKCS7Padding((const unsigned char *)data, data_len, block_size, &padded_len);
    if (!padded_data) {
        EVP_CIPHER_CTX_free(ctx);
        return NULL;
    }

    unsigned char *encrypted = (unsigned char *)malloc(padded_len);
    if (!encrypted) {
        free(padded_data);
        EVP_CIPHER_CTX_free(ctx);
        return NULL;
    }

    int out_len1 = 0;
    if (EVP_EncryptUpdate(ctx, encrypted, &out_len1, padded_data, padded_len) != 1) {
        free(padded_data);
        free(encrypted);
        EVP_CIPHER_CTX_free(ctx);
        return NULL;
    }

    int out_len2 = 0;
    if (EVP_EncryptFinal_ex(ctx, encrypted + out_len1, &out_len2) != 1) {
        free(padded_data);
        free(encrypted);
        EVP_CIPHER_CTX_free(ctx);
        return NULL;
    }

    int encrypted_len = out_len1 + out_len2;

    char *hex = (char *)malloc(encrypted_len * 2 + 1);
    if (!hex) {
        free(padded_data);
        free(encrypted);
        EVP_CIPHER_CTX_free(ctx);
        return NULL;
    }

    for (int i = 0; i < encrypted_len; i++) {
        sprintf(hex + i * 2, "%02X", encrypted[i]);
    }
    hex[encrypted_len * 2] = '\0';

    free(padded_data);
    free(encrypted);
    EVP_CIPHER_CTX_free(ctx);
    return hex;
}

// 4.4获取云盘 AccessToken 接口
// params 需要使用私钥，目前我们还没有配置，所以暂时无法按这个方向调试了
int clound_driver_access_token(const char *access_token, const char *session_secret) {
    uuid_t uuid;
    char uuid_str[UUID_STR_LEN /*37*/];  // UUID字符串形式长度为36，加1位结束符
    const char *url = "https://api.cloud.189.cn/newOpen/oauth2/accessToken.action";
    struct curl_slist *headers = NULL;

    char request_id[UUID_STR_LEN + 20] = {0};
    int ret = snprintf(request_id, sizeof(request_id), "%s", "X-Request-ID: ");

    // 生成UUID
    uuid_generate(uuid);

    // 将UUID转换为字符串形式
    uuid_unparse(uuid, request_id + ret);

    printf("Generated UUID: %s\n", request_id + ret);

    headers = curl_slist_append(headers, request_id);

    // payload
    // appKey: APP_ID
    // timeStamp: time_ms
    // accessToken:
    // paras: 使用XXTea加密方式对接口定义的所有参数（除appKey,timeStamp,accessToken,sign）拼接后的字符串加密，如params=XXTea((e189AccessToken=xxx&参数2=xxx), appSecret)，其中appSecret是申请应用时平台生成的应用秘钥，参数拼接无顺序要求。
    //        e189AccessToken
    //
    // headers = curl_slist_append(headers, "appKey: "APP_ID);
    long long t = time_ms();

    size_t encrypted_len;
    char tmp[512] = {0};

    char *payload = NULL;
    char *sign = NULL;
    // only e189AccessToken=xxx
    snprintf(tmp, sizeof(tmp), "e189AccessToken=%s", access_token);
    printf("tmp:%s\n", tmp);
    char *encrypted = xxtea_encrypt(tmp, strlen(tmp), session_secret, &encrypted_len);
    printf("paras:%s\n", encrypted);

    char *paras = (char *)malloc(encrypted_len * 2 + 1);
    if (!paras) {
        // free
        return -1;
    }

    for (int i = 0; i < encrypted_len; i++) {
        sprintf(paras + i * 2, "%02X", encrypted[i]);
    }
    paras[encrypted_len * 2] = '\0';

    printf("hex:%s\n", paras);

    asprintf(&payload, "appKey:%s&timeStamp=%lld&paras=%s&sign=%s", APP_ID, time_ms(), paras, sign);
    free(paras);

    return 0;

    struct _memory_buffer buffer;

    memory_buffer_alloc(&buffer, 2048);

    ret = http_post(url, headers, payload, strlen(payload), &buffer);
    printf("ret:%d\n", ret);
    if (ret == 0) {
        printf("response json data:%s\n", buffer.data);
        // ret = j2sobject_deserialize(result, buffer.data);
        printf("ret:%d\n", ret);
    }
    memory_buffer_free(&buffer);

    return 0;
}

int http_gmt_date(char *buffer, size_t length) {
    time_t rawtime;
    struct tm *timeinfo;

    if (buffer == NULL) {
        return -1;
    }

    time(&rawtime);
    timeinfo = gmtime(&rawtime);  // 获取UTC时间

    if (strftime(buffer, length, "%a, %d %b %Y %H:%M:%S GMT", timeinfo) == 0) {
        return -1;
    }

    return 0;
}

void to_uppercase(char *str) {
    while (*str) {
        *str = toupper((unsigned char)*str);
        str++;
    }
}
#if 0
char *extract_url_path(const char *url) {
    regex_t regex;
    regmatch_t matches[2];
    char *pattern = "://[^/]+((/[^/\\s?#]+)*)";
    if (regcomp(&regex, pattern, REG_EXTENDED)) {
        return NULL;
    }

    if (regexec(&regex, url, 2, matches, 0)) {
        regfree(&regex);
        return NULL;
    }

    int len = matches[1].rm_eo - matches[1].rm_so;
    char *result = (char *)malloc(len + 1);
    if (!result) {
        regfree(&regex);
        return NULL;
    }
    strncpy(result, url + matches[1].rm_so, len);
    result[len] = '\0';

    regfree(&regex);
    return result;
}

#else
char* extract_url_path(const char *url) {
    const char *start = strstr(url, "://");
    if (!start) {
        return NULL;
    }
    start += 3; // 跳过 "://"
    const char *path_start = strchr(start, '/');
    if (!path_start) {
        path_start = "/";
    }

    const char *query_start = strchr(path_start, '?');
    size_t path_len = query_start ? (size_t)(query_start - path_start) : strlen(path_start);

    char *path = (char*)malloc(path_len + 1);
    if (!path) {
        return NULL;
    }
    strncpy(path, path_start, path_len);
    path[path_len] = '\0';
    return path;
}
#endif

char *signatureOfHmac(const char *sessionSecret, const char *sessionKey, const char *operate, const char *fullUrl, const char *dateOfGmt, const char *param) {
    char *urlPath = extract_url_path(fullUrl);
    if (!urlPath) {
        return NULL;
    }

    printf("url %s -> %s\n", fullUrl, urlPath);
    int data_len = strlen(sessionKey) + strlen(operate) + strlen(urlPath) + strlen(dateOfGmt) + 50;
    if (param && strlen(param) > 0) {
        data_len += strlen(param) + 9;  // +9 for "&params="
    }

    char *data = (char *)malloc(data_len);
    if (!data) {
        free(urlPath);
        return NULL;
    }

    if (param && strlen(param) > 0) {
        snprintf(data, data_len, "SessionKey=%s&Operate=%s&RequestURI=%s&Date=%s&params=%s", sessionKey, operate, urlPath, dateOfGmt, param);
    } else {
        snprintf(data, data_len, "SessionKey=%s&Operate=%s&RequestURI=%s&Date=%s", sessionKey, operate, urlPath, dateOfGmt);
    }

    printf("data:%s\n", data);
    free(urlPath);

    unsigned char *result;
    unsigned int len = SHA_DIGEST_LENGTH;

    result = HMAC(EVP_sha1(), sessionSecret, strlen(sessionSecret), (unsigned char *)data, strlen(data), NULL, NULL);
    free(data);

    char *hex_result = (char *)malloc(len * 2 + 1);
    if (!hex_result) {
        return NULL;
    }

    for (unsigned int i = 0; i < len; i++) {
        sprintf(hex_result + i * 2, "%02X", result[i]);
    }
    hex_result[len * 2] = '\0';

    to_uppercase(hex_result);
    return hex_result;
}
// Referer: https://cloud.189.cn
// Sessionkey: 2f66d7f1-af6a-4a7b-a3b9-e81640c0b44d
// Signature: 3D40EFF921EB4110B73386D195DDE7B3905646E3
// X-Request-Id: 7442917f-ffb1-472f-87ec-97e05ae1c7f9
int cloud_drive_get_file_lists(const char *secret, const char *session_key) {
    uuid_t uuid;
    char request_id[UUID_STR_LEN + 20] = {0};
    char *url = NULL;
    // const char* url = "https://api.cloud.189.cn/newOpen/oauth2/accessToken.action";
    struct curl_slist *headers = NULL;

    char tmp[512] = {0};

    headers = curl_slist_append(headers, "Accept: application/json;charset=UTF-8");
    headers = curl_slist_append(headers, "Referer: https://cloud.189.cn");
    snprintf(tmp, sizeof(tmp), "Sessionkey: %s", session_key);
    headers = curl_slist_append(headers, tmp);

    // 生成UUID
    uuid_generate(uuid);

    int ret = snprintf(request_id, sizeof(request_id), "%s", "X-Request-ID: ");
    // 将UUID转换为字符串形式
    uuid_unparse(uuid, request_id + ret);

    printf("Generated UUID: %s\n", request_id + ret);

    headers = curl_slist_append(headers, request_id);

    // generate query payload

    long folder_id = -11;
    const int page_size = 100;
    int page_num = 1;

    //    CURLU *url = curl_url();
    // curl_url_set(url, CURLUPART_URL, "https://api.cloud.189.cn/newOpen/oauth2/accessToken.action");
    // snprintf(tmp, sizeof(tmp))
    // "https://api.cloud.189.cn/newOpen/oauth2/accessToken.action?folderId=%d"
    // 
    asprintf(&url,
             "https://api.cloud.189.cn/listFiles.action"
             // "http://10.30.11.78/listFiles.action"
             "?folderId=%ld"
             "&fileType=0"
             "&mediaType=0"
             "&mediaAttr=0"
             "&iconOption=0"
             "&orderBy=filename"
             "&descending=true"
             "&pageNum=%d"
             "&pageSize=%d"
             "&clientType=%s&version=%s&channelId=%s&rand=%d_%d",
             folder_id, page_num, page_size,
             PC, VERSION, CHANNEL_ID, rand(), rand());

    char date[64] = {0};
    http_gmt_date(date, sizeof(date));
    printf("date:%s\n", date);

    snprintf(tmp, sizeof(tmp), "Date: %s", date);
    headers = curl_slist_append(headers, tmp);

    char *signature = signatureOfHmac(secret, session_key, "GET", url, date, NULL);

    printf("signature:%s\n", signature);

    int offset = strlen("Signature: ");

    signature = realloc(signature, strlen(signature) + offset);

    memcpy(signature + offset, signature, strlen(signature));
    memcpy(signature, "Signature: ", offset);
    headers = curl_slist_append(headers, signature);

    struct _memory_buffer buffer;

    memory_buffer_alloc(&buffer, 2048);

    ret = driver_http_get(url, headers, NULL, 0, &buffer);

    printf("file list: %s\n", buffer.data);
    memory_buffer_free(&buffer);
    curl_slist_free_all(headers);
    free(url);
    return 0;
}
int main(int argc, char **argv) {
    // const char *access_token = "5861c56dd9c944108438cc48b0756a6e";
    // const char *secret = "6E56131239410017348D50FF3D00D425";
    // return clound_driver_access_token(access_token, secret);
#if 0
    const char *publicKey =
        "-----BEGIN PUBLIC KEY-----\nMIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQDY7mpaUysvgQkbp0iIn2ezoUyh\n"
        "i1zPFn0HCXloLFWT7uoNkqtrphpQ/63LEcPz1VYzmDuDIf3iGxQKzeoHTiVMSmW6\n"
        "FlhDeqVOG094hFJvZeK4OzA6HVwzwnEW5vIZ7d+u61RV1bsFxmB68+8JXs3ycGcE\n"
        "4anY+YzZJcyOcEGKVQIDAQAB\n-----END PUBLIC KEY-----";
#else
    // const char *publicKey = "-----BEGIN PUBLIC KEY-----\nMIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQDY7mpaUysvgQkbp0iIn2ezoUyhi1zPFn0HCXloLFWT7uoNkqtrphpQ/63LEcPz1VYzmDuDIf3iGxQKzeoHTiVMSmW6FlhDeqVOG094hFJvZeK4OzA6HVwzwnEW5vIZ7d+u61RV1bsFxmB68+8JXs3ycGcE4anY+YzZJcyOcEGKVQIDAQAB\n-----END PUBLIC KEY-----";
    const char *publicKey = "-----BEGIN PUBLIC KEY-----\nMIGfMA0GCSqGSIb3DQEBAQUAA4GNADCBiQKBgQCZLyV4gHNDUGJMZoOcYauxmNEsKrc0TlLeBEVVIIQNzG4WqjimceOj5R9ETwDeeSN3yejAKLGHgx83lyy2wBjvnbfm/nLObyWwQD/09CmpZdxoFYCH6rdDjRpwZOZ2nXSZpgkZXoOBkfNXNxnN74aXtho2dqBynTw3NFTWyQl8BQIDAQAB\n-----END PUBLIC KEY-----";
#endif
    request_login();

    char *captchaToken = extract_value(detect_data.data, "name='captchaToken' value='([^']+)'");
    char *lt = extract_value(detect_data.data, "var lt = \"([^\"]+)\"");
    char *paramId = extract_value(detect_data.data, "var paramId = \"([^\"]+)\"");
    char *reqId = extract_value(detect_data.data, "var reqId = \"([^\"]+)\"");
    char *jRsaKey = extract_value(detect_data.data, "id=\"j_rsaKey\" value=\"([^\"]+)\"");

    // Print extracted parameters
    printf("CaptchaToken: %s\n", captchaToken);
    printf("Lt: %s\n", lt);
    printf("ParamId: %s\n", paramId);
    printf("ReqId: %s\n", reqId);
    printf("jRsaKey: %s\n", jRsaKey);

    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json;charset=UTF-8");

    char *payload = "AppId: " APPID;

    memory_buffer_reset(&detect_data);

    j2sobject_encrypt_resp_t *resp = J2SENCRYPT_RESP_NEW();
    int ret = http_post_j2sobject_result(AUTH_URL "/api/logbox/config/encryptConf.do", headers, payload, strlen(payload), J2SOBJECT(resp));

    printf("result:%d, encrypt config:%s\n", ret, detect_data.data);

    printf("response result:%d\n", resp->result);
    printf("pubkey:%s\n", resp->data.pubKey);

    // j2sobject_free(J2SOBJECT(resp));
    // resp = NULL;

    // return 0;//
    char *rsaUsername = NULL;
    char *rsaPassword = NULL;

    char *aes_key = NULL;
    asprintf(&aes_key, "-----BEGIN PUBLIC KEY-----\n%s\n-----END PUBLIC KEY-----", resp->data.pubKey);
    publicKey = resp->data.pubKey;

    rsaUsername = RsaEncrypt(aes_key, "18663792866");
    rsaPassword = RsaEncrypt(aes_key, "sMiling,.556");

    printf("rsa username:%s\n", rsaUsername);
    printf("rsa password:%s\n", rsaPassword);
    rsaUsername = realloc((void *)rsaUsername, strlen(rsaUsername) + 5 + 1);
    memcpy(rsaUsername + 5, rsaUsername, strlen(rsaUsername));
    memcpy(rsaUsername, "{NRP}", 5);

    rsaPassword = realloc((void *)rsaPassword, strlen(rsaPassword) + 5 + 1);
    memcpy(rsaPassword + 5, rsaPassword, strlen(rsaPassword));
    memcpy(rsaPassword, "{NRP}", 5);

    printf("rsa username:%s\n", rsaUsername);
    printf("rsa password:%s\n", rsaPassword);
    performLogin(reqId, lt, rsaUsername, rsaPassword, captchaToken, "", paramId);

    char *to_url = extract_value(detect_data.data, "\"toUrl\":\"([^\"]+)\"");
    printf("to url:%s\n", to_url);

    char secret[256] = {0};
    char session[256] = {0};

    printf("secret:%s\n", secret);
    printf("session:%s\n", session);
    request_session_token(to_url, secret, session);

    cloud_drive_get_file_lists(secret, session);
    free(captchaToken);
    free(lt);
    free(paramId);
    free(reqId);
    free(jRsaKey);
    // const char *str = RsaEncrypt(publicKey, "18663792866");
    return 0;
}