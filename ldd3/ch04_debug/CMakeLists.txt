cmake_minimum_required(VERSION 3.21)
project(scull_debug NONE)

kmod_target(scull_proc
        SOURCES proc.c
        HEADERS debug.h
        # EXTRA_CFLAGS "-Wall -Wextra -DDEBUG"
        EXTRA_CFLAGS "-I${CMAKE_CURRENT_SOURCE_DIR} -DSCULL_DEBUG" # include headers in cwd
)

kmod_target(scull_proc_seq_iter
        SOURCES proc_seq_iter.c
        HEADERS debug.h
        # EXTRA_CFLAGS "-Wall -Wextra -DDEBUG"
        EXTRA_CFLAGS "-I${CMAKE_CURRENT_SOURCE_DIR} -DSCULL_DEBUG" # include headers in cwd
)


kmod_target(scull_proc_seq_snapshot
        SOURCES proc_seq_snapshot.c
        HEADERS debug.h
        # EXTRA_CFLAGS "-Wall -Wextra -DDEBUG"
        EXTRA_CFLAGS "-I${CMAKE_CURRENT_SOURCE_DIR} -DSCULL_DEBUG" # include headers in cwd
)

kmod_target(scull_sysfs
        SOURCES sysfs.c
        HEADERS debug.h
        # EXTRA_CFLAGS "-Wall -Wextra -DDEBUG"
        EXTRA_CFLAGS "-I${CMAKE_CURRENT_SOURCE_DIR} -DSCULL_DEBUG" # include headers in cwd
)