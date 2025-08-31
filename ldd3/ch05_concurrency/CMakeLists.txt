cmake_minimum_required(VERSION 3.21)
project(concurrency NONE)
kmod_target(scull_completion
        SOURCES completion.c
        HEADERS
        # EXTRA_CFLAGS "-Wall -Wextra -DDEBUG"
        EXTRA_CFLAGS "-I${CMAKE_CURRENT_SOURCE_DIR}" # include headers in cwd
)
kmod_target(scull_rwsem
        SOURCES rwsem.c
        HEADERS
        # EXTRA_CFLAGS "-Wall -Wextra -DDEBUG"
        EXTRA_CFLAGS "-I${CMAKE_CURRENT_SOURCE_DIR}" # include headers in cwd
)