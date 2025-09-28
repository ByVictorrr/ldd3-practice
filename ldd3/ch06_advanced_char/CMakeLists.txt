cmake_minimum_required(VERSION 3.21)
project(advanced_scull NONE)
set(COMMON_SCULL_DIR "${CMAKE_SOURCE_DIR}/ldd3/common_scull")
kmod_target(advanced_scull
        SOURCES
            main.c
            ${COMMON_SCULL_DIR}/scull_core.c
            scull_pipe.c
            scull_access_control.c
        HEADERS
            ${COMMON_SCULL_DIR}/scull.h
            ${COMMON_SCULL_DIR}/scull_ioctl.h
            scull_pipe.h
            scull_access_control.h
        # EXTRA_CFLAGS "-Wall -Wextra -DDEBUG"
        EXTRA_CFLAGS "-Wno-error=format -g -fno-omit-frame-pointer" # include headers in cwd
)
# Userspace tester for scull access devices
add_executable(scull_access_tester
        test.c
)