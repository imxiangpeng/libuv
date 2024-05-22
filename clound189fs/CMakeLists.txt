cmake_minimum_required(VERSION 3.10)

project(clound189fs LANGUAGES C)

FIND_LIBRARY(libuv_library NAMES uv)
FIND_LIBRARY(cjson_library NAMES cjson)
FIND_LIBRARY(curl_library NAMES curl)
FIND_LIBRARY(uuid_library NAMES uuid)

find_package(LibXml2 REQUIRED)

INCLUDE_DIRECTORIES(j2s)

SET(SOURCES clound189fs.c j2s/j2sobject.c j2s/j2stable.c j2sobject_response.c xxtea.c)

# ADD_DEFINITIONS(-Wall -Wextra -Werror)
ADD_DEFINITIONS(-g )

ADD_EXECUTABLE(clound189fs clound189fs.c ${SOURCES})
TARGET_LINK_LIBRARIES(clound189fs ${libuv_library})
TARGET_LINK_LIBRARIES(clound189fs ${cjson_library})

TARGET_LINK_LIBRARIES(clound189fs ${curl_library})

target_include_directories(clound189fs PRIVATE ${LIBXML2_INCLUDE_DIRS})
target_link_libraries(clound189fs ${LIBXML2_LIBRARY})
target_link_libraries(clound189fs -lssl -lcrypto)
TARGET_LINK_LIBRARIES(clound189fs ${uuid_library})


INCLUDE_DIRECTORIES(.)

ADD_EXECUTABLE(cloud_j2sobject tests/cloud_j2sobject.c j2s/j2sobject.c j2sobject_cloud.c)
TARGET_LINK_LIBRARIES(cloud_j2sobject ${cjson_library})
