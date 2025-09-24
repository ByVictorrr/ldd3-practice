cmake_minimum_required(VERSION 3.21)
project(advanced_scull NONE)
kmod_target(advanced_scull
        SOURCES   main.c scull_core.c scull_pipe.c scull_access_control.c
        HEADERS scull_pipe.h scull_ioctl.h scull.h scull_access_control.h
        # EXTRA_CFLAGS "-Wall -Wextra -DDEBUG"
        EXTRA_CFLAGS "-I${CMAKE_CURRENT_SOURCE_DIR}" # include headers in cwd
)
# Userspace tester for scull access devices
add_executable(scull_access_tester
        test.c
)