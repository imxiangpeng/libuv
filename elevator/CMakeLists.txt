cmake_minimum_required(VERSION 3.10)

project(elevator_sensor LANGUAGES C)

FIND_LIBRARY(libuv_library NAMES uv)
FIND_LIBRARY(cjson_library NAMES cjson)
FIND_LIBRARY(libiio_library NAMES iio)
FIND_LIBRARY(libuv_library NAMES uv)
FIND_LIBRARY(libmosquitto_library NAMES mosquitto)


#find_package(LibXml2 REQUIRED)


option(USE_LOCAL_SIMULATE_DATA "Runing using local simulate data" OFF)

#INCLUDE_DIRECTORIES(j2s)

# ADD_DEFINITIONS(-Wall -Wextra -Werror)
ADD_DEFINITIONS(-g )
INCLUDE_DIRECTORIES(.)
INCLUDE_DIRECTORIES(sensors)

set(SOURCES main.c core.c hr_log.c butterworth_filter.c tui.c time_utils.c file_util.c
    sensors/sensor.c sensors/sensor_bmi270.c sensors/sensor_bmp388.c
    accelerometer_motion.c
    floor.c)

if(USE_LOCAL_SIMULATE_DATA)
list(APPEND SOURCES sensors/sensor_simulate.c)
endif()

ADD_EXECUTABLE(elevatord ${SOURCES})

if(USE_LOCAL_SIMULATE_DATA)
TARGET_COMPILE_DEFINITIONS(elevatord PRIVATE USE_LOCAL_SIMULATE_DATA=1)
endif()


TARGET_LINK_LIBRARIES(elevatord ${cjson_library})
TARGET_LINK_LIBRARIES(elevatord ${mosquitto_library})
TARGET_LINK_LIBRARIES(elevatord ${libuv_library})
TARGET_LINK_LIBRARIES(elevatord ${libiio_library})

TARGET_LINK_LIBRARIES(elevatord m)
TARGET_LINK_LIBRARIES(elevatord pthread)
TARGET_LINK_LIBRARIES(elevatord ncurses)

#INSTALL(TARGETS elevatord RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
