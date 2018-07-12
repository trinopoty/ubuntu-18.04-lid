cmake_minimum_required(VERSION 3.10)
project(systemd_lid C)

set(CMAKE_C_STANDARD 99)

FIND_PATH(
        UDEV_INCLUDE_DIR
        libudev.h
        /usr/include
        /usr/local/include
        ${UDEV_PATH_INCLUDES}
        )
FIND_LIBRARY(
        UDEV_LIBRARIES
        NAMES udev libudev
        PATHS
        /usr/lib${LIB_SUFFIX}
        /usr/local/lib${LIB_SUFFIX}
        ${UDEV_PATH_LIB}
        )

add_executable(systemd_lid
        main.c
        lidManager.c
        button.c
        power.c)
target_link_libraries(systemd_lid systemd)

target_include_directories(systemd_lid PUBLIC ${UDEV_INCLUDE_DIR})
target_link_libraries(systemd_lid ${UDEV_LIBRARIES})
