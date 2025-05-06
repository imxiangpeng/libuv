cmake_minimum_required(VERSION 3.10)

project(iutils LANGUAGES C)

# ADD_DEFINITIONS(-Wall -Wextra -Werror)
INCLUDE_DIRECTORIES(.)
INCLUDE_DIRECTORIES(include)

option(HRLOG_OUTPUT_FILE "output fille output to file not default stdout" OFF)

set(SOURCES file_util.c
    hr_buffer.c
    hr_log.c
    time_utils.c)

ADD_LIBRARY(iutils STATIC ${SOURCES})
target_include_directories(iutils PUBLIC .)

target_compile_options(iutils PRIVATE -fvisibility=hidden)


