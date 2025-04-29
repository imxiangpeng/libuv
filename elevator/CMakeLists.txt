cmake_minimum_required(VERSION 3.10)

project(elevator_sensor LANGUAGES C)

FIND_LIBRARY(libuv_library NAMES uv)
FIND_LIBRARY(cjson_library NAMES cjson)
FIND_LIBRARY(libiio_library NAMES iio)
FIND_LIBRARY(libuv_library NAMES uv)
FIND_LIBRARY(libmosquitto_library NAMES mosquitto)


#find_package(LibXml2 REQUIRED)

add_subdirectory(uobject)

option(USE_LOCAL_SIMULATE_DATA "Runing using local simulate data" OFF)

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
    tui/tui.c 
    utils/time_utils.c
    utils/file_util.c
    utils/hr_log.c 
    iot/iot.c
    iot/iot_mosquitto.c
    iot/iot_topic.c
    iot/iot_topic_property.c
    iot/iot_topic_calibration.c
    uobject/uelevatord.c
    uobject/uobject_elevatord.c
    butterworth_filter.c
    hr_buffer.c
    moving_window.c)

if(USE_LOCAL_SIMULATE_DATA)
list(APPEND SOURCES sensors/sensor_simulate.c)
endif()

ADD_EXECUTABLE(elevatord ${SOURCES})

if(USE_LOCAL_SIMULATE_DATA)
TARGET_COMPILE_DEFINITIONS(elevatord PRIVATE USE_LOCAL_SIMULATE_DATA=1)
endif()

TARGET_COMPILE_OPTIONS(elevatord PRIVATE -Wall -Wextra -Werror -Wno-unused-function)

TARGET_LINK_LIBRARIES(elevatord ${cjson_library})
TARGET_LINK_LIBRARIES(elevatord ${mosquitto_library})
TARGET_LINK_LIBRARIES(elevatord ${libuv_library})
TARGET_LINK_LIBRARIES(elevatord ${libiio_library})

TARGET_LINK_LIBRARIES(elevatord m)
TARGET_LINK_LIBRARIES(elevatord pthread)
TARGET_LINK_LIBRARIES(elevatord ncurses)
TARGET_LINK_LIBRARIES(elevatord -lssl -lcrypto)
TARGET_LINK_LIBRARIES(elevatord -lfftw3)
TARGET_LINK_LIBRARIES(elevatord uobject)

#INSTALL(TARGETS elevatord RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
