cmake_minimum_required(VERSION 3.21)
project(scull_debug NONE)
kmod_target(scull_debug
        SOURCES
            proc.c

        HEADERS
            debug.h
        # EXTRA_CFLAGS "-Wall -Wextra -DDEBUG"
        EXTRA_CFLAGS "-I${CMAKE_CURRENT_SOURCE_DIR}" # include headers in cwd
)
