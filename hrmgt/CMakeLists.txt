cmake_minimum_required(VERSION 3.10)

project(hrmgt LANGUAGES C)

FIND_LIBRARY(libuv_library NAMES uv)

SET(SOURCES hrmgt.c)

ADD_EXECUTABLE(hrmgt ${SOURCES})

TARGET_LINK_LIBRARIES(hrmgt ${libuv_library})

