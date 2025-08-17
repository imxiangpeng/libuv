cmake_minimum_required(VERSION 3.10)

project(elevator_sensor LANGUAGES C)

FIND_LIBRARY(libuv_library NAMES uv)
FIND_LIBRARY(cjson_library NAMES cjson)
FIND_LIBRARY(libiio_library NAMES iio)
FIND_LIBRARY(libuv_library NAMES uv)
FIND_LIBRARY(libmosquitto_library NAMES mosquitto)
FIND_LIBRARY(ubus_library NAMES ubus PATHS /opt/3rdparty/lib NO_DEFAULT_PATH)
FIND_LIBRARY(ubox_library NAMES ubox PATHS /opt/3rdparty/lib NO_DEFAULT_PATH)
FIND_LIBRARY(blob_library NAMES blobmsg_json PATHS /opt/3rdparty/lib NO_DEFAULT_PATH)

FIND_LIBRARY(libuv_library NAMES uv)

#find_package(LibXml2 REQUIRED)

option(USE_LOCAL_SIMULATE_DATA "Runing using local simulate data" OFF)
option(DUMP_DATA_TO_FILE "Dump result to local csv file" OFF)
option(ENABLE_IOT "Support iot feature" OFF)

string(TIMESTAMP BUILD_TIMESTAMP "%Y-%m-%d %H:%M:%S")

#INCLUDE_DIRECTORIES(j2s)

# ADD_DEFINITIONS(-Wall -Wextra -Werror)
ADD_DEFINITIONS(-g )
INCLUDE_DIRECTORIES(.)
INCLUDE_DIRECTORIES(include)

set(SOURCES main.c
    motion.c
    floor.c
    accelerometer_stream.c
    barometer_stream.c
    sensors/sensor.c
    sensors/sensor_bmi270.c
    sensors/sensor_bmp388.c
    tui.c 
    ubus/uelevatord.c
    butterworth_filter.c
    moving_window.c)

if(USE_LOCAL_SIMULATE_DATA)
list(APPEND SOURCES sensors/sensor_simulate.c)
endif()

if(ENABLE_IOT)
list(APPEND SOURCES iot/iot.c
    iot/iot_topic.c
    iot/iot_topic_property.c
    iot/iot_topic_service.c)
endif()

ADD_EXECUTABLE(elevatord ${SOURCES})

TARGET_COMPILE_DEFINITIONS(elevatord PRIVATE LOG_TAG="elevatord")
# define config file when compile
#TARGET_COMPILE_DEFINITIONS(elevatord PRIVATE ELEVATORD_CONFIG_PATH="/etc/elevatord/elevatord.conf")
#TARGET_COMPILE_DEFINITIONS(elevatord PRIVATE HQLIFTD_CONFIG_PATH="/etc/hqliftd/hqliftd.conf")

TARGET_COMPILE_DEFINITIONS(elevatord PRIVATE ELEVATORD_CONFIG_PATH="config/elevatord/elevatord.conf")
TARGET_COMPILE_DEFINITIONS(elevatord PRIVATE HQLIFTD_CONFIG_PATH="config/hqliftd/hqliftd.conf")
TARGET_COMPILE_DEFINITIONS(elevatord PRIVATE EGUARD_CONFIG_PATH="/etc/eguard/eguard.conf")

TARGET_COMPILE_DEFINITIONS(elevatord PRIVATE DEFAULT_ACCELEROMETER_SAMPLING_RATE_HZ=200)


if(USE_LOCAL_SIMULATE_DATA)
TARGET_COMPILE_DEFINITIONS(elevatord PRIVATE USE_LOCAL_SIMULATE_DATA=1)
endif()

if(ENABLE_IOT)
TARGET_COMPILE_DEFINITIONS(elevatord PRIVATE ENABLE_IOT=1)
endif()

if(DUMP_DATA_TO_FILE)
TARGET_COMPILE_DEFINITIONS(elevatord PRIVATE DUMP_DATA_TO_FILE=1)
else()
TARGET_COMPILE_DEFINITIONS(elevatord PRIVATE DUMP_DATA_TO_FILE=0)
endif()

TARGET_COMPILE_DEFINITIONS(elevatord PRIVATE ELEVATORD_BUILD_TIMESTAMP="${BUILD_TIMESTAMP}")

TARGET_COMPILE_OPTIONS(elevatord PRIVATE -Wall -Wextra -Werror -Wno-unused-function)

# only on my pc
target_include_directories(elevatord PUBLIC /opt/3rdparty/include)

TARGET_LINK_LIBRARIES(elevatord ${cjson_library})
TARGET_LINK_LIBRARIES(elevatord ${libmosquitto_library})
TARGET_LINK_LIBRARIES(elevatord ${libuv_library})
TARGET_LINK_LIBRARIES(elevatord ${libiio_library})

TARGET_LINK_LIBRARIES(elevatord m)
TARGET_LINK_LIBRARIES(elevatord pthread)
TARGET_LINK_LIBRARIES(elevatord ncurses)
TARGET_LINK_LIBRARIES(elevatord -lssl -lcrypto)
TARGET_LINK_LIBRARIES(elevatord -lfftw3)
TARGET_LINK_LIBRARIES(elevatord ${ubox_library} ${ubus_library} ${blob_library})
TARGET_LINK_LIBRARIES(elevatord unwind unwind-generic)
# sudo apt install libdw-dev
TARGET_LINK_LIBRARIES(elevatord dw elf)

TARGET_LINK_LIBRARIES(elevatord uviot)
TARGET_LINK_LIBRARIES(elevatord iutils)
TARGET_LINK_LIBRARIES(elevatord platform)

#INSTALL(TARGETS elevatord RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
