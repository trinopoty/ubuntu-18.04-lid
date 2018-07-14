cmake_minimum_required(VERSION 3.10)
project(systemd-lid C)

set(CMAKE_C_STANDARD 99)

find_package(PkgConfig REQUIRED)
pkg_check_modules(UDEV libudev)
pkg_check_modules(DCONF dconf)

add_executable(systemd-lid
        main.c
        lidManager.c
        button.c
        power.c)
target_link_libraries(systemd-lid systemd)

target_include_directories(systemd-lid PUBLIC ${UDEV_INCLUDE_DIRS})
link_directories(${UDEV_LIBRARY_DIRS})
target_link_libraries(systemd-lid ${UDEV_LIBRARIES})

target_include_directories(systemd-lid PUBLIC ${DCONF_INCLUDE_DIRS})
link_directories(${DCONF_LIBRARY_DIRS})
target_link_libraries(systemd-lid ${DCONF_LIBRARIES})