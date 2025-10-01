cmake_minimum_required(VERSION 3.21)
project(scullv NONE)

kmod_target(scullv
        SOURCES scullv.c
        # EXTRA_CFLAGS "-Wall -Wextra -DDEBUG"
        EXTRA_CFLAGS "-I${CMAKE_CURRENT_SOURCE_DIR} -DSCULL_DEBUG" # include headers in cwd
)


