cmake_minimum_required(VERSION 3.21)
project(scull_mem NONE)
set(COMMON_SCULL_DIR "${CMAKE_SOURCE_DIR}/ldd3/common_scull")
kmod_target(scullc
        SOURCES scullc.c
        HEADERS  ${COMMON_SCULL_DIR}/scull.h
        # EXTRA_CFLAGS "-Wall -Wextra -DDEBUG"
        EXTRA_CFLAGS "-I${CMAKE_CURRENT_SOURCE_DIR} -DSCULL_DEBUG" # include headers in cwd
)
kmod_target(scullp
        SOURCES scullp.c

        HEADERS  ${COMMON_SCULL_DIR}/scull.h
        # EXTRA_CFLAGS "-Wall -Wextra -DDEBUG"
        EXTRA_CFLAGS "-I${CMAKE_CURRENT_SOURCE_DIR} -DSCULL_DEBUG" # include headers in cwd
)

kmod_target(scullv
        SOURCES scullv.c
        HEADERS  ${COMMON_SCULL_DIR}/scull.h
        # EXTRA_CFLAGS "-Wall -Wextra -DDEBUG"
        EXTRA_CFLAGS "-I${CMAKE_CURRENT_SOURCE_DIR} -DSCULL_DEBUG" # include headers in cwd
)



