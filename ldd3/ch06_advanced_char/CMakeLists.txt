cmake_minimum_required(VERSION 3.21)
project(scull_access NONE)
kmod_target(scull_access
        SOURCES scull_access_control.c scull_pipe.c scull_core.c main.c
        HEADERS scull_pipe.h scull_pipe.h scull.h
        # EXTRA_CFLAGS "-Wall -Wextra -DDEBUG"
        EXTRA_CFLAGS "-I${CMAKE_CURRENT_SOURCE_DIR}" # include headers in cwd
)