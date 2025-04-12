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

ADD_EXECUTABLE(iot dm_main.c hr_log.c 
                board_platform
                dm_topic dm_topic_heartbeat
                dm_topic_command
                dm_topic_liftstate
                dm_topic_liftfault
                dm_topic_liftruninfo)

TARGET_LINK_LIBRARIES(iot ${cjson_library})
TARGET_LINK_LIBRARIES(iot ${mosquitto_library})
TARGET_LINK_LIBRARIES(iot ${libuv_library})
TARGET_LINK_LIBRARIES(iot -lssl -lcrypto)