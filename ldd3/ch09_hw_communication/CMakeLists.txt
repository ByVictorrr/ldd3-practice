cmake_minimum_required(VERSION 3.21)
project(hardware_communicating NONE)
set(COMMON_SCULL_DIR "${CMAKE_SOURCE_DIR}/ldd3/common_scull")
kmod_target(short
        SOURCES short_port.c
        HEADERS  ${COMMON_SCULL_DIR}/scull.h
        # EXTRA_CFLAGS "-Wall -Wextra -DDEBUG"
        EXTRA_CFLAGS "-I${CMAKE_CURRENT_SOURCE_DIR} -DSCULL_DEBUG" # include headers in cwd
)


