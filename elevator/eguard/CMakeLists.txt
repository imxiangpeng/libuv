cmake_minimum_required(VERSION 3.10)

project(eguard LANGUAGES C)

FIND_LIBRARY(libuv_library NAMES uv)
FIND_LIBRARY(cjson_library NAMES cjson)
FIND_LIBRARY(avformat_library NAMES avformat)
FIND_LIBRARY(avcodec_library NAMES avcodec)

FIND_LIBRARY(ubus_library NAMES ubus PATHS /opt/3rdparty/lib NO_DEFAULT_PATH)
FIND_LIBRARY(ubox_library NAMES ubox PATHS /opt/3rdparty/lib NO_DEFAULT_PATH)
FIND_LIBRARY(blob_library NAMES blobmsg_json PATHS /opt/3rdparty/lib NO_DEFAULT_PATH)
FIND_LIBRARY(uuid_library NAMES uuid)

# ADD_DEFINITIONS(-Wall -Wextra -Werror)
ADD_DEFINITIONS(-g )
INCLUDE_DIRECTORIES(.)
INCLUDE_DIRECTORIES(include)

set(SOURCES eguard)

ADD_EXECUTABLE(eguard ${SOURCES})

TARGET_COMPILE_OPTIONS(eguard PRIVATE -Wall -Wextra -Werror -Wno-unused-function)

TARGET_COMPILE_DEFINITIONS(eguard PRIVATE LOG_TAG="eguard")
# define config file when compile
TARGET_COMPILE_DEFINITIONS(eguard PRIVATE EGUARD_CONFIG_PATH="/etc/hqliftd/hqliftd.conf")

# only on my pc
# target_include_directories(hqliftd PUBLIC /opt/3rdparty/include)


TARGET_LINK_LIBRARIES(eguard ${libuv_library})
TARGET_LINK_LIBRARIES(eguard ${avformat_library} ${avcodec_library})


TARGET_LINK_LIBRARIES(eguard pthread)
TARGET_LINK_LIBRARIES(eguard -lssl -lcrypto)

TARGET_LINK_LIBRARIES(eguard iutils)
