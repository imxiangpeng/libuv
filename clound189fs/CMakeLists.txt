cmake_minimum_required(VERSION 3.10)

project(clound189fs LANGUAGES C)

FIND_LIBRARY(libuv_library NAMES uv)
FIND_LIBRARY(cjson_library NAMES cjson)
FIND_LIBRARY(curl_library NAMES curl)

find_package(LibXml2 REQUIRED)

INCLUDE_DIRECTORIES(j2s)

SET(SOURCES clound189fs.c j2s/j2sobject.c j2s/j2stable.c)

# ADD_DEFINITIONS(-Wall -Wextra -Werror)
ADD_DEFINITIONS(-g )

ADD_EXECUTABLE(clound189fs clound189fs.c ${SOURCES})
TARGET_LINK_LIBRARIES(clound189fs ${libuv_library})
TARGET_LINK_LIBRARIES(clound189fs ${cjson_library})


