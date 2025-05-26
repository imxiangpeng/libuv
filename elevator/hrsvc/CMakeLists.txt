cmake_minimum_required(VERSION 3.10)

PROJECT(hrsvc C)

ADD_EXECUTABLE(hrsvc hrsvc.c)


target_compile_definitions(hrsvc PRIVATE HRSVC_UNIX_SOCK="/var/hrsvcd.sock")


ADD_EXECUTABLE(hrsvcd hrsvcd.c)
target_compile_definitions(hrsvcd PRIVATE HRSVC_UNIX_SOCK="/var/hrsvcd.sock")
target_compile_definitions(hrsvcd PRIVATE HRSVC_CONFIG_DIR="/etc/hrsvc.d")
TARGET_LINK_LIBRARIES(hrsvcd hr_base)

# sample code, default do not install.
INSTALL(TARGETS hrsvc RUNTIME DESTINATION bin)
INSTALL(TARGETS hrsvcd RUNTIME DESTINATION sbin)

install(FILES hrsvc.d/ubusd.json DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/hrsvc.d)


