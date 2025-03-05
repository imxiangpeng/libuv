cmake_minimum_required(VERSION 3.10)

project(unicom_router LANGUAGES C)

FIND_LIBRARY(libuv_library NAMES uv)
FIND_LIBRARY(cjson_library NAMES cjson)
FIND_LIBRARY(curl_library NAMES curl)
FIND_LIBRARY(uuid_library NAMES uuid)
FIND_LIBRARY(jsonc_library NAMES json-c)
FIND_LIBRARY(mosquitto_library NAMES mosquitto)


#find_package(LibXml2 REQUIRED)

#INCLUDE_DIRECTORIES(j2s)

# ADD_DEFINITIONS(-Wall -Wextra -Werror)
ADD_DEFINITIONS(-g )

INCLUDE_DIRECTORIES(.)

ADD_EXECUTABLE(dm_lift dm_main.c hr_log.c dm_topic dm_topic_heartbeat
                dm_topic_command)

TARGET_LINK_LIBRARIES(dm_lift ${cjson_library})
TARGET_LINK_LIBRARIES(dm_lift ${mosquitto_library})
