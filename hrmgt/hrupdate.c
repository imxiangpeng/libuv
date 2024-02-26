#include <curl/curl.h>
#include <fcntl.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <openssl/evp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <uv.h>

#ifndef TEMP_FAILURE_RETRY
#define TEMP_FAILURE_RETRY(exp)                \
    ({                                         \
        typeof(exp) _rc;                       \
        do {                                   \
            _rc = (exp);                       \
        } while (_rc == -1 && errno == EINTR); \
        _rc;                                   \
    })
#endif

// a simple message loop async function
struct _state {
    int state;
};

struct hrupdate_desc {
    char version[24];
    long size;
    char md5[32 + 4];  // 32 + 1
    char url[512];
};

struct hrupdate {
    int fd;
    char path[128];
    EVP_MD_CTX *mdctx;
    struct hrupdate_desc desc;
};
void _async_cb(uv_async_t *handle) {
    printf("%s(%d): handle:%p\n", __FUNCTION__, __LINE__, handle);
}

static void work_cb(uv_work_t *req) {
    printf("%s(%d): req:%p\n", __FUNCTION__, __LINE__, req);
}

static void after_work_cb(uv_work_t *req, int status) {
    printf("%s(%d): req:%p, status:%d\n", __FUNCTION__, __LINE__, req, status);
}

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
        memory_buffer_realloc(buf, buf->size + total + 1);
    }

    memcpy((void *)(buf->data + buf->offset), ptr, total);
    buf->offset += total;

    // printf("data:%s\n", buf->data);
    return total;
}

static ssize_t _write_file_fd(int fd, char *data, size_t size) {
    ssize_t left = size;
    char *ptr = data;

    if (fd < 0 || !data || size == 0) return -1;

    while (left > 0) {
        ssize_t n = TEMP_FAILURE_RETRY(write(fd, ptr, left));
        if (n == -1) {
            close(fd);
            return size - left;
        }
        ptr += n;
        left -= n;
    }

    return size;
}

static size_t _download_receive(void *ptr, size_t size, size_t nmemb,
                                void *userdata) {
    (void)ptr;
    struct hrupdate *update = (struct hrupdate *)userdata;

    size_t total = size * nmemb;

    if (!update) {
        return -1;
    }

    if (update->fd < 0) {
        int fd = open(update->path, O_RDWR | O_TRUNC | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        if (fd < 0) {
            printf("open failed ... start downloading :%p ...\n", update);
            return -1;
        }
        update->fd = fd;

        update->mdctx = EVP_MD_CTX_new();
        EVP_DigestInit_ex(update->mdctx, EVP_md5(), NULL);
    }

    ssize_t ss = _write_file_fd(update->fd, ptr, total);

    if (ss > 0)
        EVP_DigestUpdate(update->mdctx, ptr, ss);

    return ss;
}
static xmlNode *firstElement(xmlNode *node, const xmlChar *tag) {
    // internal api must not null
    for (xmlNode *child = node->children; child; child = child->next) {
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

#define UPGRADE_VERSION_PROP BAD_CAST "LastVersion"
#define UPGRADE_FILE_TAG BAD_CAST "UpgradeFile"
#define DOWNLAOD_TYPE_TAG BAD_CAST "DownloadType"
#define UPGRADESOFTWARE_TAG BAD_CAST "UpgradeSoftware"

static int _detect_response_parse(struct hrupdate_desc *desc, struct _memory_buffer *buf) {
    xmlDocPtr doc = xmlReadMemory(buf->data, buf->offset, "", NULL, 0);
    if (!doc)
        return -1;

    xmlNode *root = xmlDocGetRootElement(doc);
    if (root == NULL) {
        xmlFreeDoc(doc);
        return -1;
    }

    // new version
    xmlChar *version = xmlGetProp(root, UPGRADE_VERSION_PROP);
    if (!version) {
        // maybe no need upgrade
        xmlFreeDoc(doc);
        return 0;
    }

    snprintf(desc->version, sizeof(desc->version), "%s", version);
    xmlFree(version);
    version = NULL;

    xmlNode *upgrade_file = firstElement(root, BAD_CAST UPGRADE_FILE_TAG);
    if (!upgrade_file) {
        xmlFreeDoc(doc);
        printf("can not find ... upgrade file...\n");
        return 0;
    }

    xmlNode *type = firstElement(upgrade_file, BAD_CAST DOWNLAOD_TYPE_TAG);
    xmlChar *typeval = xmlNodeGetContent(type);
    if (!type || !typeval || typeval[0] == '0') {
        xmlFree(typeval);
        xmlFreeDoc(doc);
        printf("not need upgrade ...\n");
        return 0;
    }

    xmlFree(typeval);

    xmlNode *software = firstElement(upgrade_file, BAD_CAST UPGRADESOFTWARE_TAG);
    if (!software) {
        xmlFree(typeval);
        xmlFreeDoc(doc);
        printf("invalid upgrade file tag ...\n");
        return -1;
    }
    xmlChar *fileSize = xmlGetProp(software, BAD_CAST "FileSize");
    if (fileSize) {
        desc->size = atoi((const char *)fileSize);
        xmlFree(fileSize);
    }

    xmlChar *md5 = xmlGetProp(software, BAD_CAST "MD5");
    if (md5) {
        snprintf(desc->md5, sizeof(desc->md5), "%s", md5);
        xmlFree(md5);
    }
    xmlChar *fileUrl = xmlGetProp(software, BAD_CAST "UpgradeFileUrl");
    if (fileUrl) {
        snprintf(desc->url, sizeof(desc->url), "%s", fileUrl);
        xmlFree(fileUrl);
    }

    xmlFreeDoc(doc);

    xmlCleanupParser();
    // xmlMemoryDump();
    return 0;
}

static int _update_detect(struct hrupdate_desc *desc) {
    long response_code = 0;
    CURL *curl = NULL;

    const char *url = "https://msr.inspur.com:8011/getPolicyFile?type=upgrade&deviceType=V352F_DMO1_FJYZ_MSR2023&version=1.1.1&ip=1.1.1.1&sn=LCRT1234567890&mac=00:00:00:00:00:00&areaCode=&hwVer=&cpu=&manufacture=Inspur&hardwareVersion=&soid=20";
    // const char *url = "https://msr.inspur.com:8011/getPolicyFile?type=upgrade&deviceType=V352F_DMO1_FJYZ_MSR2023&version=1.1.3&ip=1.1.1.1&sn=LCRT1234567890&mac=00:00:00:00:00:00&areaCode=&hwVer=&cpu=&manufacture=Inspur&hardwareVersion=&soid=20";

    // const char *url = "http://www.baidu.com";
    //
    //
    if (!desc) return -1;

    curl = curl_easy_init();
    if (!curl) return -1;

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_SERVER_RESPONSE_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, _data_receive);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &detect_data);

    CURLcode code = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    curl_easy_cleanup(curl);

    if (CURLE_OK != code) {
        printf("request failed ... %d:%s\n", code, curl_easy_strerror(code));
        return -1;
    }

    if (response_code != 200) return -1;

    return _detect_response_parse(desc, &detect_data);
}

#ifndef MD5_DIGEST_LENGTH
#define MD5_DIGEST_LENGTH 16
#endif

static int _update_download(struct hrupdate *update) {
    CURL *curl = NULL;
    char md5sum[MD5_DIGEST_LENGTH * 2 + 1] = {0};
    long response_code = 0;
    if (!update) return -1;

    curl = curl_easy_init();

    if (!curl) return -1;

    curl_easy_setopt(curl, CURLOPT_URL, update->desc.url);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_SERVER_RESPONSE_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, _download_receive);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, update);

    printf("start downloading :%s ...\n", update->desc.url);
    CURLcode code = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response_code);
    curl_easy_cleanup(curl);

    if (CURLE_OK != code || response_code != 200) {
        printf("request failed ... %d:%s\n", code, curl_easy_strerror(code));

        if (update->fd > 0) {
            close(update->fd);
            update->fd = -1;
        }

        // clean the file
        unlink(update->path);

        if (update->mdctx) {
            EVP_MD_CTX_free(update->mdctx);
            update->mdctx = NULL;
        }
        return -1;
    }

    if (update->fd > 0) {
        close(update->fd);
        update->fd = -1;
    }

    if (update->mdctx) {
        unsigned char *md5_digest = NULL;
        unsigned int md5_digest_len = EVP_MD_size(EVP_md5());
        char *ptr = md5sum;
        int available = sizeof(md5sum);

        md5_digest = (unsigned char *)OPENSSL_malloc(md5_digest_len);
        EVP_DigestFinal_ex(update->mdctx, md5_digest, &md5_digest_len);
        EVP_MD_CTX_free(update->mdctx);

        for (unsigned int i = 0; i < md5_digest_len; i++) {
            if (available < 2) break;
            int ret = snprintf(ptr, available, "%02X", md5_digest[i]);
            available -= ret;
            ptr += ret;
        }
        OPENSSL_free(md5_digest);
    }

    printf("md5:%s ...........\n", md5sum);

    if (strncasecmp(md5sum, update->desc.md5, strlen(update->desc.md5)) != 0) {
        printf("md5 not match!\n");
        // delete file
        // remove();
    } else {
        printf("md5 match!\n");
    }
    return 0;
}
int main(int argc, char **argv) {
    (void)argc;
    (void)argv;
    struct hrupdate update = {.fd = -1, .desc = {{0}, 0, {0}, {0}}};

    memset((void *)&update, 0, sizeof(update));
    update.fd = -1;
    snprintf(update.path, sizeof(update.path), "%s", "update.bin");

    curl_global_init(CURL_GLOBAL_DEFAULT);

    memory_buffer_alloc(&detect_data, 2048);
    int ret = _update_detect(&update.desc);
    memory_buffer_free(&detect_data);

    if (ret < 0) return -1;

    printf("desc: size:%ld, md5sum:%s, url:%s\n", update.desc.size, update.desc.md5, update.desc.url);

    if (update.desc.size == 0 || update.desc.md5[0] == '\0' || update.desc.url[0] == '\0') {
        printf("no valid update description ...\n");
        return -1;
    }

    ret = _update_download(&update);

#if 0
    uv_loop_t *main_loop = uv_default_loop();
    uv_async_t async;
    uv_work_t work;

    // default loop has been inited
    // uv_loop_init(main_loop);
    //

    uv_async_init(main_loop, &async, _async_cb);

    uv_async_send(&async);

    uv_queue_work(main_loop, &work, work_cb, after_work_cb);


    uv_run(main_loop, UV_RUN_DEFAULT);
#endif
    return 0;
}
