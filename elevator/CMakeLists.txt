cmake_minimum_required(VERSION 3.10)

project(elevator_sensor LANGUAGES C)

FIND_LIBRARY(libuv_library NAMES uv)
FIND_LIBRARY(cjson_library NAMES cjson)
#FIND_LIBRARY(curl_library NAMES curl)
#FIND_LIBRARY(uuid_library NAMES uuid)
#FIND_LIBRARY(jsonc_library NAMES json-c)
#FIND_LIBRARY(mosquitto_library NAMES mosquitto)


#find_package(LibXml2 REQUIRED)


option(USE_LOCAL_SIMULATE_DATA "Runing using local simulate data" ON)

#INCLUDE_DIRECTORIES(j2s)

# ADD_DEFINITIONS(-Wall -Wextra -Werror)
ADD_DEFINITIONS(-g )
INCLUDE_DIRECTORIES(.)

set(SOURCES main.c core.c acceleration.c barometer.c hr_log.c)

if(USE_LOCAL_SIMULATE_DATA)
list(APPEND SOURCES simulate.c)
endif()

ADD_EXECUTABLE(elevator_sensor ${SOURCES})

if(USE_LOCAL_SIMULATE_DATA)
TARGET_COMPILE_DEFINITIONS(elevator_sensor PRIVATE USE_LOCAL_SIMULATE_DATA=1)
endif()


TARGET_LINK_LIBRARIES(elevator_sensor ${cjson_library})
TARGET_LINK_LIBRARIES(elevator_sensor ${mosquitto_library})
TARGET_LINK_LIBRARIES(elevator_sensor ${libuv_library})

TARGET_LINK_LIBRARIES(elevator_sensor m)
TARGET_LINK_LIBRARIES(elevator_sensor pthread)