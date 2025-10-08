cmake_minimum_required(VERSION 3.21)
project(scull NONE)
set(COMMON_SCULL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../common_scull")

kmod_target(scull
        SOURCES
            main.c
            ${COMMON_SCULL_DIR}/scull_core.c
        HEADERS
            ${COMMON_SCULL_DIR}/scull.h
        # EXTRA_CFLAGS "-Wall -Wextra -DDEBUG"
        EXTRA_CFLAGS "-I${COMMON_SCULL_DIR} -Wno-error=misleading-indentation -g -fno-omit-frame-pointer -Wno-error=missing-prototypes" # include headers in cwd
)
# path to shared headers
add_subdirectory(test)