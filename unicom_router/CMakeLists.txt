cmake_minimum_required(VERSION 3.10)

project(directory_model LANGUAGES C)

FIND_LIBRARY(libuv_library NAMES uv)
FIND_LIBRARY(cjson_library NAMES cjson)
FIND_LIBRARY(curl_library NAMES curl)
FIND_LIBRARY(uuid_library NAMES uuid)

find_package(LibXml2 REQUIRED)

INCLUDE_DIRECTORIES(j2s)

# ADD_DEFINITIONS(-Wall -Wextra -Werror)
ADD_DEFINITIONS(-g )

ADD_EXECUTABLE(directory_model directory_model.c)
TARGET_LINK_LIBRARIES(directory_model ${libuv_library})
TARGET_LINK_LIBRARIES(directory_model ${cjson_library})

TARGET_LINK_LIBRARIES(directory_model ${curl_library})

target_include_directories(directory_model PRIVATE ${LIBXML2_INCLUDE_DIRS})
target_link_libraries(directory_model ${LIBXML2_LIBRARY})
target_link_libraries(directory_model -lssl -lcrypto)
TARGET_LINK_LIBRARIES(directory_model ${uuid_library})


INCLUDE_DIRECTORIES(.)

#ADD_EXECUTABLE(cloud_j2sobject tests/cloud_j2sobject.c j2s/j2sobject.c j2sobject_cloud.c)
#TARGET_LINK_LIBRARIES(cloud_j2sobject ${cjson_library})
