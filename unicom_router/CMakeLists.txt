cmake_minimum_required(VERSION 3.10)

project(unicom_router LANGUAGES C)

FIND_LIBRARY(libuv_library NAMES uv)
FIND_LIBRARY(cjson_library NAMES cjson)
FIND_LIBRARY(curl_library NAMES curl)
FIND_LIBRARY(uuid_library NAMES uuid)
FIND_LIBRARY(jsonc_library NAMES json-c)
FIND_LIBRARY(mosquitto_library NAMES mosquitto)


find_package(LibXml2 REQUIRED)

INCLUDE_DIRECTORIES(j2s)

# ADD_DEFINITIONS(-Wall -Wextra -Werror)
ADD_DEFINITIONS(-g )

INCLUDE_DIRECTORIES(.)

ADD_EXECUTABLE(uc_auth uc_auth.c hr_log.c hr_buffer.c url_request.c)
TARGET_LINK_LIBRARIES(uc_auth ${curl_library})
TARGET_LINK_LIBRARIES(uc_auth ${jsonc_library})

ADD_EXECUTABLE(dm_main dm_main.c hr_log.c hr_buffer.c dm_object.c dm_Device.c dm_Device_DeviceInfo.c dm_Device_X_CU_WAN.c)
# TARGET_LINK_LIBRARIES(dm_main ${curl_library})
TARGET_LINK_LIBRARIES(dm_main ${jsonc_library})
