cmake_minimum_required(VERSION 3.12)

project(estreamer LANGUAGES C)

FIND_LIBRARY(curl_library NAMES curl)
INCLUDE_DIRECTORIES(.)
INCLUDE_DIRECTORIES(include)

set(SOURCES estreamer)

ADD_EXECUTABLE(estreamer ${SOURCES})

TARGET_COMPILE_OPTIONS(estreamer PRIVATE -Wall -Wextra -Werror -Wno-unused-function)

TARGET_COMPILE_DEFINITIONS(estreamer PRIVATE LOG_TAG="estreamer")
#TARGET_COMPILE_DEFINITIONS(estreamer PRIVATE IPC_MEDIA_RECORD_DIR="/media/mmcblk0p1")

TARGET_LINK_LIBRARIES(estreamer ${curl_library})
TARGET_LINK_LIBRARIES(estreamer pthread)
TARGET_LINK_LIBRARIES(estreamer iutils)
