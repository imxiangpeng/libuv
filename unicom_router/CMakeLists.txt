cmake_minimum_required(VERSION 3.10)

project(unicom_router LANGUAGES C)

FIND_LIBRARY(libuv_library NAMES uv)
FIND_LIBRARY(cjson_library NAMES cjson)
FIND_LIBRARY(curl_library NAMES curl)
FIND_LIBRARY(uuid_library NAMES uuid)
FIND_LIBRARY(mosquitto_library NAMES mosquitto)


find_package(LibXml2 REQUIRED)

INCLUDE_DIRECTORIES(j2s)

# ADD_DEFINITIONS(-Wall -Wextra -Werror)
ADD_DEFINITIONS(-g )

ADD_EXECUTABLE(uc_data_model uc_data_model.c hr_log.c)
TARGET_LINK_LIBRARIES(uc_data_model ${libuv_library})
TARGET_LINK_LIBRARIES(uc_data_model ${cjson_library})

TARGET_LINK_LIBRARIES(uc_data_model ${curl_library})

target_include_directories(uc_data_model PRIVATE ${LIBXML2_INCLUDE_DIRS})
target_link_libraries(uc_data_model ${LIBXML2_LIBRARY})
target_link_libraries(uc_data_model -lssl -lcrypto)
TARGET_LINK_LIBRARIES(uc_data_model ${uuid_library})
TARGET_LINK_LIBRARIES(uc_data_model ${mosquitto_library})


INCLUDE_DIRECTORIES(.)

ADD_EXECUTABLE(uc_auth uc_auth.c hr_log.c hr_buffer.c url_request.c)
TARGET_LINK_LIBRARIES(uc_auth ${curl_library})
