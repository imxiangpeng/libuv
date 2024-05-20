#include <curl/urlapi.h>
#define _GNU_SOURCE
#include <curl/curl.h>
#include <errno.h>
#include <fcntl.h>
#include <regex.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

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
    memset((void*)((const char*)buf->data + buf->offset), 0, buf->size - buf->offset);
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

static size_t _data_receive(void *ptr, size_t size, size_t nmemb,
                            void *userdata) {
    struct _memory_buffer *buf = (struct _memory_buffer *)userdata;

    size_t total = size * nmemb;

    if (!ptr || !userdata)
        return -1;

    if (buf->size - buf->offset < total) {
        printf("we should enlarge data\n\n\n\n");
        memory_buffer_realloc(buf, buf->size + total + 1);
    }

    memcpy((void *)(buf->data + buf->offset), ptr, total);
    buf->offset += total;

    printf("dd:%s\n\n", (const char*)ptr);
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

static int url_get(CURLU *url, /*struct curl_slist *data, struct curl_slist *header, */ struct _memory_buffer *mem) {
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

#define WEB_BASE_URL "https://cloud.189.cn"
#define APPID "8025431004"
#define CLI_TYPE "10020"
#define RETURN_URL "https://m.cloud.189.cn/zhuanti/2020/loginErrorPc/index.html"
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

    url_get(url, &detect_data);

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

int main(int argc, char **argv) {
    request_login();
    return 0;
}