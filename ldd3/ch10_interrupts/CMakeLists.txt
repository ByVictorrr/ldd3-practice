cmake_minimum_required(VERSION 3.21)
project(interrupts NONE)
kmod_target(short_intr
        SOURCES short_intr.c
        # MODULE_PARAMS "use_mem=0"
        # EXTRA_CFLAGS "-Wall -Wextra -DDEBUG"
        EXTRA_CFLAGS "-I${CMAKE_CURRENT_SOURCE_DIR} -DSCULL_DEBUG" # include headers in cwd
)
