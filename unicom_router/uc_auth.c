#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#include "hr_buffer.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hr_list.h"
#include "hr_log.h"
#include "url_request.h"

// #define MNG_URL "https://rtm.ossapp.chinaunicom.cn:8803"
#define MNG_URL "https://123.6.50.69:8803"

#define MNG_URL_AUTH MNG_URL "/api/auth"
#define MNG_URL_DEVICE_GET_MQTT_SERVER MNG_URL "/api/deviceGetMqttServer"

int main(int argc, char** argv) {
    printf("%s(%d): ......\n", __FUNCTION__, __LINE__);

    struct hrbuffer b;

    struct url_request* req = url_request_new();

    const char* data = "{\n"
"	\"id\": \"52\",\n"
"	\"cwmpVersion\": \"2.0\",\n"
"	\"method\": \"devAuth\",\n"
"	\"devId\": \"\",\n"
"	\"secret\": \"\",\n"
"	\"parameterValues\": {\n"
"		\"Device\": {\n"
"			\"DeviceInfo\": {\n"
"				\"ModelName\": \"V5870-ZN\",\n"
"				\"SerialNumber\": \"7485E22E42E3BD3A\",\n"
"				\"SoftwareVersion\": \"V1.0.3\",\n"
"				\"X_CU_FriendlyName\": \"E8820-SH-0a:c7:86\",\n"
"				\"X_CU_CUEI\": \"123456789012345\"\n"
"			},\n"
"			\"X_CU_WAN\": {\n"
"				\"MAC\": \"48:28:2f:89:7c:52\",\n"
"				\"ConnectionType\": \"DHCP\"\n"
"			}\n"
"		}\n"
"	}\n"
"}";
    
    printf("%s\n", data);
    hrbuffer_alloc(&b, 512);

    req->set_header(req, "Content-Type", "application/json;charset=UTF-8");

    req->set_form(req, data, NULL);
    req->post(req, MNG_URL_AUTH, &b);

    printf("response:%s\n", b.data);
    hrbuffer_free(&b);
    url_request_free(req);
    return 0;
}