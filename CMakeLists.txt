cmake_minimum_required(VERSION 3.10)
project(gnome3-lid C)

set(CMAKE_C_STANDARD 99)

find_package(PkgConfig REQUIRED)
pkg_check_modules(UDEV libudev)
pkg_check_modules(DCONF dconf)

add_executable(gnome3-lid
        main.c
        lidManager.c
        button.c
        power.c)
target_link_libraries(gnome3-lid systemd)

target_include_directories(gnome3-lid PUBLIC ${UDEV_INCLUDE_DIRS})
link_directories(${UDEV_LIBRARY_DIRS})
target_link_libraries(gnome3-lid ${UDEV_LIBRARIES})

target_include_directories(gnome3-lid PUBLIC ${DCONF_INCLUDE_DIRS})
link_directories(${DCONF_LIBRARY_DIRS})
target_link_libraries(gnome3-lid ${DCONF_LIBRARIES})