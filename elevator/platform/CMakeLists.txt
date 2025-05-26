cmake_minimum_required(VERSION 3.10)

project(platform LANGUAGES C)

# ADD_DEFINITIONS(-Wall -Wextra -Werror)
INCLUDE_DIRECTORIES(.)
INCLUDE_DIRECTORIES(include)

set(SOURCES platform.h
    platform_amlogic.c)

ADD_LIBRARY(platform STATIC ${SOURCES})
target_include_directories(platform PUBLIC .)

target_compile_options(platform PRIVATE -fvisibility=hidden)
