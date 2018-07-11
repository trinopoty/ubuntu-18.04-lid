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

#find_package (PkgConfig REQUIRED)
#pkg_check_modules (GIO REQUIRED gio-2.0)

add_executable(systemd_lid main.c)
target_link_libraries(systemd_lid systemd)

target_include_directories(systemd_lid PUBLIC ${UDEV_INCLUDE_DIR})
target_link_libraries(systemd_lid ${UDEV_LIBRARIES})

#target_include_directories(systemd_lid PUBLIC ${GIO_INCLUDE_DIRS})
#link_directories(${GIO_LIBRARY_DIRS})
#target_link_libraries(systemd_lid ${GIO_LIBRARIES})