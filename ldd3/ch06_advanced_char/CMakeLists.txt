cmake_minimum_required(VERSION 3.21)
project(scull NONE)
kmod_target(scull_advanced
        SOURCES main.c scull_core.c
        HEADERS scull.h scull_ioctl.h
        # EXTRA_CFLAGS "-Wall -Wextra -DDEBUG"
        EXTRA_CFLAGS "-I${CMAKE_CURRENT_SOURCE_DIR}" # include headers in cwd
)