cmake_minimum_required(VERSION 3.10)

project(econsoled LANGUAGES C)


FIND_LIBRARY(cjson_library NAMES cjson)
FIND_LIBRARY(libmosquitto_library NAMES mosquitto)
FIND_LIBRARY(ubus_library NAMES ubus PATHS /opt/3rdparty/lib NO_DEFAULT_PATH)
FIND_LIBRARY(ubox_library NAMES ubox PATHS /opt/3rdparty/lib NO_DEFAULT_PATH)
FIND_LIBRARY(blob_library NAMES blobmsg_json PATHS /opt/3rdparty/lib NO_DEFAULT_PATH)

string(TIMESTAMP BUILD_TIMESTAMP "%Y-%m-%d %H:%M:%S")

# ADD_DEFINITIONS(-Wall -Wextra -Werror)
ADD_DEFINITIONS(-g )
INCLUDE_DIRECTORIES(.)
INCLUDE_DIRECTORIES(include)

set(SOURCES main
    econsoled)

ADD_EXECUTABLE(econsoled ${SOURCES})

TARGET_COMPILE_OPTIONS(econsoled PRIVATE -Wall -Wextra -Werror -Wno-unused-function)

TARGET_COMPILE_DEFINITIONS(econsoled PRIVATE LOG_TAG="econsoled")
# define config file when compile
#TARGET_COMPILE_DEFINITIONS(econsoled PRIVATE ECONSOLED_CONFIG_PATH="/etc/hqliftd/hqliftd.conf")
TARGET_COMPILE_DEFINITIONS(econsoled PRIVATE ECONSOLED_CONFIG_PATH="econsoled.conf")

TARGET_COMPILE_DEFINITIONS(econsoled PRIVATE BUILD_TIMESTAMP="${BUILD_TIMESTAMP}")


# only on my pc
target_include_directories(econsoled PUBLIC /opt/3rdparty/include)

TARGET_LINK_LIBRARIES(econsoled ${cjson_library})
TARGET_LINK_LIBRARIES(econsoled ${libmosquitto_library})

TARGET_LINK_LIBRARIES(econsoled m)
TARGET_LINK_LIBRARIES(econsoled pthread)
TARGET_LINK_LIBRARIES(econsoled -lssl -lcrypto)
TARGET_LINK_LIBRARIES(econsoled ${ubox_library} ${ubus_library} ${blob_library})
TARGET_LINK_LIBRARIES(econsoled ${uuid_library})
TARGET_LINK_LIBRARIES(econsoled ${curl_library})

#INSTALL(TARGETS econsoled RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
