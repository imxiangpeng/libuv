cmake_minimum_required(VERSION 3.10)

project(uviot LANGUAGES C)

FIND_LIBRARY(libuv_library NAMES uv)
FIND_LIBRARY(cjson_library NAMES cjson)
FIND_LIBRARY(libmosquitto_library NAMES mosquitto)

# ADD_DEFINITIONS(-Wall -Wextra -Werror)
ADD_DEFINITIONS(-g )
INCLUDE_DIRECTORIES(.)
INCLUDE_DIRECTORIES(include)

set(SOURCES main.c
    iot_topic.c
    hr_log.c
                 topic_houqi_heartbeat
                topic_houqi_command
                topic_houqi_liftstate
                topic_houqi_liftfault
                topic_houqi_liftruninfo   
)

ADD_LIBRARY(uviot STATIC uviot.c)

target_compile_options(uviot PRIVATE -fvisibility=hidden)
TARGET_LINK_LIBRARIES(uviot ${libmosquitto_library})

ADD_EXECUTABLE(uviot_main ${SOURCES})

TARGET_COMPILE_OPTIONS(uviot_main  PRIVATE -Wall -Wextra -Werror -Wno-unused-function)

# only on my pc
target_include_directories(uviot_main  PUBLIC /opt/3rdparty/include)

TARGET_LINK_LIBRARIES(uviot_main  ${cjson_library})
TARGET_LINK_LIBRARIES(uviot_main  ${libmosquitto_library})
TARGET_LINK_LIBRARIES(uviot_main  ${libuv_library})

TARGET_LINK_LIBRARIES(uviot_main  m)
TARGET_LINK_LIBRARIES(uviot_main  pthread)
TARGET_LINK_LIBRARIES(uviot_main  uviot)


#INSTALL(TARGETS uviot_main  RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
