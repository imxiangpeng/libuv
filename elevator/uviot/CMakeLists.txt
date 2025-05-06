cmake_minimum_required(VERSION 3.10)

project(uviot LANGUAGES C)

FIND_LIBRARY(libuv_library NAMES uv)
FIND_LIBRARY(cjson_library NAMES cjson)
FIND_LIBRARY(libmosquitto_library NAMES mosquitto)

# ADD_DEFINITIONS(-Wall -Wextra -Werror)

INCLUDE_DIRECTORIES(.)
INCLUDE_DIRECTORIES(../iutils)

ADD_LIBRARY(uviot STATIC uviot.c ../iutils/hr_log.c)
target_include_directories(uviot PUBLIC .)

target_compile_options(uviot PRIVATE -fvisibility=hidden)
TARGET_LINK_LIBRARIES(uviot ${libmosquitto_library})
TARGET_LINK_LIBRARIES(uviot ${libuv_library})
# TARGET_LINK_LIBRARIES(uviot iutils)


add_subdirectory(examples)

