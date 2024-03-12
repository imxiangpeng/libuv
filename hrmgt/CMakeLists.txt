cmake_minimum_required(VERSION 3.10)

project(hrmgt LANGUAGES C)

FIND_LIBRARY(libuv_library NAMES uv)
FIND_LIBRARY(cjson_library NAMES cjson)
FIND_LIBRARY(curl_library NAMES curl)

find_package(LibXml2 REQUIRED)

INCLUDE_DIRECTORIES(j2s)

SET(SOURCES hr_service.h j2s/j2sobject.c j2s/j2stable.c)

# ADD_DEFINITIONS(-Wall -Wextra -Werror)
ADD_DEFINITIONS(-g )

ADD_EXECUTABLE(hrmgt hrmgt.c ${SOURCES})
TARGET_LINK_LIBRARIES(hrmgt ${libuv_library})
TARGET_LINK_LIBRARIES(hrmgt ${cjson_library})

ADD_EXECUTABLE(hrtbl_test hrtbl_main.c ${SOURCES})

ADD_EXECUTABLE(hrupdate hrupdate.c ${SOURCES})
TARGET_LINK_LIBRARIES(hrupdate ${libuv_library} ${curl_library})
TARGET_LINK_LIBRARIES(hrupdate ${cjson_library})
target_include_directories(hrupdate PRIVATE ${LIBXML2_INCLUDE_DIRS})
# target_link_libraries(hrupdate PRIVATE LibXml2::LibXml2)
target_link_libraries(hrupdate ${LIBXML2_LIBRARY})
target_link_libraries(hrupdate -lssl -lcrypto)
TARGET_COMPILE_OPTIONS(hrupdate PRIVATE -Wall -Wextra -Werror -Wno-error=unused-function)


ADD_EXECUTABLE(hrweekalarm hrweekalarm.c ${SOURCES})
TARGET_LINK_LIBRARIES(hrweekalarm ${cjson_library})

ADD_EXECUTABLE(hrtbl_action hrtable_action.c ${SOURCES})
TARGET_LINK_LIBRARIES(hrtbl_action ${libuv_library})
TARGET_LINK_LIBRARIES(hrtbl_action ${cjson_library})

ADD_EXECUTABLE(hrtbl_schedule hrtbl_schedule.c)
#TARGET_LINK_LIBRARIES(hrtbl_schedule ${libuv_library})
#TARGET_LINK_LIBRARIES(hrtbl_schedule ${cjson_library})
ADD_LIBRARY(hrtbl_schedule_module_crontask SHARED hrtbl_schedule_crontask.c ${SOURCES})
TARGET_LINK_LIBRARIES(hrtbl_schedule_module_crontask ${cjson_library})



ADD_EXECUTABLE(hrsvcd hrsvcd.c ${SOURCES})
TARGET_LINK_LIBRARIES(hrsvcd ${cjson_library})

ADD_EXECUTABLE(hrsvc hrsvc.c)

ADD_EXECUTABLE(http_postfile http_postfile.c)


TARGET_COMPILE_OPTIONS(hrtbl_test PRIVATE -Wall -Wextra -Werror -Wno-error=unused-function)
TARGET_LINK_LIBRARIES(hrtbl_test ${cjson_library})
