cmake_minimum_required(VERSION 3.10)

PROJECT(uobject C)

# RELEASE OR DEBUG
SET(CMAKE_BUILD_TYPE RELEASE)

FIND_LIBRARY(ubus_library NAMES ubus PATHS /opt/3rdparty/lib NO_DEFAULT_PATH)
FIND_LIBRARY(ubox_library NAMES ubox PATHS /opt/3rdparty/lib NO_DEFAULT_PATH)
FIND_LIBRARY(blob_library NAMES blobmsg_json PATHS /opt/3rdparty/lib NO_DEFAULT_PATH)

INCLUDE_DIRECTORIES(./)
INCLUDE_DIRECTORIES(../include)

SET(SOURCES "uobject.c")

ADD_DEFINITIONS(-Wall -Wextra -Werror -Wno-error=unused-function -Wno-error=unused-result)
ADD_LIBRARY(uobject SHARED ${SOURCES})

target_include_directories(uobject PUBLIC .)
target_include_directories(uobject PUBLIC /opt/3rdparty/include)

TARGET_LINK_LIBRARIES(uobject ${ubox_library} ${ubus_library} ${blob_library})



