cmake_minimum_required(VERSION 3.10)

project(hqlift LANGUAGES C)

FIND_LIBRARY(libuv_library NAMES uv)
FIND_LIBRARY(cjson_library NAMES cjson)
FIND_LIBRARY(libiio_library NAMES iio)
FIND_LIBRARY(libuv_library NAMES uv)
FIND_LIBRARY(libmosquitto_library NAMES mosquitto)
FIND_LIBRARY(ubus_library NAMES ubus PATHS /opt/3rdparty/lib NO_DEFAULT_PATH)
FIND_LIBRARY(ubox_library NAMES ubox PATHS /opt/3rdparty/lib NO_DEFAULT_PATH)
FIND_LIBRARY(blob_library NAMES blobmsg_json PATHS /opt/3rdparty/lib NO_DEFAULT_PATH)

# ADD_DEFINITIONS(-Wall -Wextra -Werror)
ADD_DEFINITIONS(-g )
INCLUDE_DIRECTORIES(.)
INCLUDE_DIRECTORIES(include)

set(SOURCES main.c
    time_utils.c
    file_util.c
    hr_log.c 
    iot.c
    iot_mosquitto.c
    iot_topic.c
                topic_houqi_heartbeat
                topic_houqi_command
                topic_houqi_liftstate
                topic_houqi_liftfault
                topic_houqi_liftruninfo
    uelevator.c
    platform.c
    elevator.c)

ADD_EXECUTABLE(hqlift ${SOURCES})

TARGET_COMPILE_OPTIONS(hqlift PRIVATE -Wall -Wextra -Werror -Wno-unused-function)

# only on my pc
target_include_directories(hqlift PUBLIC /opt/3rdparty/include)

TARGET_LINK_LIBRARIES(hqlift ${cjson_library})
TARGET_LINK_LIBRARIES(hqlift ${libmosquitto_library})
TARGET_LINK_LIBRARIES(hqlift ${libuv_library})
TARGET_LINK_LIBRARIES(hqlift ${libiio_library})

TARGET_LINK_LIBRARIES(hqlift m)
TARGET_LINK_LIBRARIES(hqlift pthread)
TARGET_LINK_LIBRARIES(hqlift -lssl -lcrypto)
TARGET_LINK_LIBRARIES(hqlift ${ubox_library} ${ubus_library} ${blob_library})


#INSTALL(TARGETS hqlift RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
