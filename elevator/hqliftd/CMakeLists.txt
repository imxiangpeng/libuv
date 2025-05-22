cmake_minimum_required(VERSION 3.10)

project(hqlift LANGUAGES C)

FIND_LIBRARY(libuv_library NAMES uv)
FIND_LIBRARY(cjson_library NAMES cjson)
FIND_LIBRARY(libmosquitto_library NAMES mosquitto)
FIND_LIBRARY(ubus_library NAMES ubus PATHS /opt/3rdparty/lib NO_DEFAULT_PATH)
FIND_LIBRARY(ubox_library NAMES ubox PATHS /opt/3rdparty/lib NO_DEFAULT_PATH)
FIND_LIBRARY(blob_library NAMES blobmsg_json PATHS /opt/3rdparty/lib NO_DEFAULT_PATH)
FIND_LIBRARY(uuid_library NAMES uuid)

# ADD_DEFINITIONS(-Wall -Wextra -Werror)
ADD_DEFINITIONS(-g )
INCLUDE_DIRECTORIES(.)
INCLUDE_DIRECTORIES(include)

set(SOURCES main
    iot
    iot_topic
    topic_houqi_heartbeat
    topic_houqi_command
    topic_houqi_liftstate
    topic_houqi_liftfault
    topic_houqi_liftruninfo
    uelevator
    platform
    elevator
    state_machine)

ADD_EXECUTABLE(hqliftd ${SOURCES})

TARGET_COMPILE_OPTIONS(hqliftd PRIVATE -Wall -Wextra -Werror -Wno-unused-function)

# only on my pc
target_include_directories(hqliftd PUBLIC /opt/3rdparty/include)

TARGET_LINK_LIBRARIES(hqliftd ${cjson_library})
#TARGET_LINK_LIBRARIES(hqliftd ${libmosquitto_library})
TARGET_LINK_LIBRARIES(hqliftd ${libuv_library})

TARGET_LINK_LIBRARIES(hqliftd m)
TARGET_LINK_LIBRARIES(hqliftd pthread)
TARGET_LINK_LIBRARIES(hqliftd -lssl -lcrypto)
TARGET_LINK_LIBRARIES(hqliftd ${ubox_library} ${ubus_library} ${blob_library})
TARGET_LINK_LIBRARIES(hqliftd ${uuid_library})

TARGET_LINK_LIBRARIES(hqliftd uviot)
TARGET_LINK_LIBRARIES(hqliftd iutils)

#INSTALL(TARGETS hqliftd RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
