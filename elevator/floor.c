#include <math.h>
#include <stdlib.h>
#include <cmath>
#include "file_util.h"
#include "hr_log.h"

#include "cjson/cJSON.h"

int floor_load_model(const char *path) {
    int ret = -1;
    ssize_t len = 0;
    char *data = NULL;
cJSON *root = NULL;
    if (!path) {
        return -1;
    }

    len =futil_read(path, &data);
    if (len <= 0) {
        return -1;
    }
    

    root = cJSON_ParseWithLength(data, len);
    if (!root) {
        HR_LOGE("can not read file:%s, data:%s\n", path, data);

        HR_LOGE("error:%s\n", cJSON_GetErrorPtr());
        free(data);
        return -1;
    }
    const char* version = cJSON_GetStringValue(cJSON_GetObjectItem(root, "version"));
    const char* date = cJSON_GetStringValue(cJSON_GetObjectItem(root, "date"));
    double base = cJSON_GetNumberValue(cJSON_GetObjectItem(root, "base"));
    if (!version || !date || isnan(base)) {
        free(data);
        return -1;
    }

    cJSON_Delete(root);

    free(data);
    return ret;
}

int floor_init() {
    
}