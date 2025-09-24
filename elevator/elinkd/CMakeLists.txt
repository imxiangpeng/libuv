cmake_minimum_required(VERSION 3.10)

project(elinkd LANGUAGES C)


FIND_LIBRARY(cjson_library NAMES cjson)
FIND_LIBRARY(jsonc_library NAMES json-c)
FIND_LIBRARY(libmosquitto_library NAMES mosquitto)
FIND_LIBRARY(ubus_library NAMES ubus PATHS /opt/3rdparty/lib NO_DEFAULT_PATH)
FIND_LIBRARY(ubox_library NAMES ubox PATHS /opt/3rdparty/lib NO_DEFAULT_PATH)
FIND_LIBRARY(blob_library NAMES blobmsg_json PATHS /opt/3rdparty/lib NO_DEFAULT_PATH)

string(TIMESTAMP BUILD_TIMESTAMP "%Y-%m-%d %H:%M:%S")

# ADD_DEFINITIONS(-Wall -Wextra -Werror)
ADD_DEFINITIONS(-g )
INCLUDE_DIRECTORIES(.)
INCLUDE_DIRECTORIES(include)
INCLUDE_DIRECTORIES(../iutils)
INCLUDE_DIRECTORIES(../platform)


set(SOURCES main
    elinkd
    iot
    topic
    topic_property
    topic_service
    topic_event
    topic_download
    property
    service
    system
    elevator
    ../iutils/hr_log
    ../iutils/file_util
    ../iutils/hr_buffer
    ../iutils/sconf
    ../platform/platform_amlogic)

ADD_EXECUTABLE(elinkd ${SOURCES})

TARGET_COMPILE_OPTIONS(elinkd PRIVATE -Wall -Wextra -Werror -Wno-unused-function)

TARGET_COMPILE_DEFINITIONS(elinkd PRIVATE LOG_TAG="elinkd" ENABLE_HRLOG_PROTO_FILE=1)
# define config file when compile
#TARGET_COMPILE_DEFINITIONS(elinkd PRIVATE ELINKD_CONFIG_PATH="/etc/elinkd/elinkd.conf")
TARGET_COMPILE_DEFINITIONS(elinkd PRIVATE ELINKD_CONFIG_PATH="elinkd.conf")
#TARGET_COMPILE_DEFINITIONS(elinkd PRIVATE ELEVATORD_CONFIG_PATH="/etc/elevatord/elevatord.conf")
# TARGET_COMPILE_DEFINITIONS(elinkd PRIVATE ELEVATORD_CONFIG_PATH="elevatord.conf")
#TARGET_COMPILE_DEFINITIONS(elinkd PRIVATE ELEVATORD_FLOOR_MODEL_PATH="/etc/elevatord/floor_model.json")
TARGET_COMPILE_DEFINITIONS(elinkd PRIVATE ELEVATORD_FLOOR_MODEL_PATH="floor_model.json")
TARGET_COMPILE_DEFINITIONS(elinkd PRIVATE EGUARD_CONFIG_PATH="/etc/eguard/eguard.conf")

#TARGET_COMPILE_DEFINITIONS(elinkd PRIVATE HQLIFTD_CONFIG_PATH="/etc/hqliftd/hqliftd.conf")
TARGET_COMPILE_DEFINITIONS(elinkd PRIVATE HQLIFTD_CONFIG_PATH="hqliftd.conf")
#TARGET_COMPILE_DEFINITIONS(elinkd PRIVATE ELOG_CONFIG_PATH="/etc/elog.conf")
TARGET_COMPILE_DEFINITIONS(elinkd PRIVATE ELOG_CONFIG_PATH="config/elog.conf")
TARGET_COMPILE_DEFINITIONS(elinkd PRIVATE HRLOG_CONFIG_PATH="config/elog.conf")


TARGET_COMPILE_DEFINITIONS(elinkd PRIVATE BUILD_TIMESTAMP="${BUILD_TIMESTAMP}")

TARGET_COMPILE_DEFINITIONS(elinkd PRIVATE IPC_MEDIA_RECORD_DIR="/media/mmcblk0p1")

# only on my pc
target_include_directories(elinkd PUBLIC /opt/3rdparty/include)

TARGET_LINK_LIBRARIES(elinkd ${cjson_library})
TARGET_LINK_LIBRARIES(elinkd ${jsonc_library})
TARGET_LINK_LIBRARIES(elinkd ${libmosquitto_library})

TARGET_LINK_LIBRARIES(elinkd m)
TARGET_LINK_LIBRARIES(elinkd pthread)
TARGET_LINK_LIBRARIES(elinkd -lssl -lcrypto)
TARGET_LINK_LIBRARIES(elinkd ${ubox_library} ${ubus_library} ${blob_library})
TARGET_LINK_LIBRARIES(elinkd ${uuid_library})
TARGET_LINK_LIBRARIES(elinkd ${curl_library})

#INSTALL(TARGETS elinkd RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
