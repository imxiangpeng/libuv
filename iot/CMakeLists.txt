cmake_minimum_required(VERSION 3.10)

project(unicom_router LANGUAGES C)

FIND_LIBRARY(libuv_library NAMES uv)
FIND_LIBRARY(cjson_library NAMES cjson)
#FIND_LIBRARY(curl_library NAMES curl)
#FIND_LIBRARY(uuid_library NAMES uuid)
#FIND_LIBRARY(jsonc_library NAMES json-c)
FIND_LIBRARY(mosquitto_library NAMES mosquitto)


#find_package(LibXml2 REQUIRED)

INCLUDE_DIRECTORIES(j2s)

# ADD_DEFINITIONS(-Wall -Wextra -Werror)
ADD_DEFINITIONS(-g )

INCLUDE_DIRECTORIES(.)

ADD_EXECUTABLE(iot_mosquitto iot_mosquitto.c hr_log.c 
                board_platform
                dm_topic
                dm_topic_property)

TARGET_LINK_LIBRARIES(iot_mosquitto ${cjson_library})
TARGET_LINK_LIBRARIES(iot_mosquitto ${mosquitto_library})
TARGET_LINK_LIBRARIES(iot_mosquitto ${libuv_library})
TARGET_LINK_LIBRARIES(iot_mosquitto -lssl -lcrypto)