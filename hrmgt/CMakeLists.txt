cmake_minimum_required(VERSION 3.10)

project(hrmgt LANGUAGES C)

FIND_LIBRARY(libuv_library NAMES uv)
FIND_LIBRARY(cjson_library NAMES cjson)

INCLUDE_DIRECTORIES(j2s)

SET(SOURCES hr_service.h j2s/j2sobject.c hr_tbl.c)

# ADD_DEFINITIONS(-Wall -Wextra -Werror)

ADD_EXECUTABLE(hrmgt hrmgt.c ${SOURCES})
TARGET_LINK_LIBRARIES(hrmgt ${libuv_library})
TARGET_LINK_LIBRARIES(hrmgt ${cjson_library})

ADD_EXECUTABLE(hrtbl_test hrtbl_main.c ${SOURCES})

TARGET_COMPILE_OPTIONS(hrtbl_test PRIVATE -Wall -Wextra -Werror -Wno-error=unused-function)
TARGET_LINK_LIBRARIES(hrtbl_test ${cjson_library})
