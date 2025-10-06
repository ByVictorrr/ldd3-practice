cmake_minimum_required(VERSION 3.21)
project(hardware_communicating NONE)
kmod_target(short_port
        SOURCES short.c
        MODULE_PARAMS "use_mem=0"
        # EXTRA_CFLAGS "-Wall -Wextra -DDEBUG"
        EXTRA_CFLAGS "-I${CMAKE_CURRENT_SOURCE_DIR} -DSCULL_DEBUG" # include headers in cwd
)

# doesnt work really no mmio device to control
kmod_target(short_mem
        SOURCES short.c
        MODULE_PARAMS "use_mem=1 short_base=0xFEBF0000"
        # EXTRA_CFLAGS "-Wall -Wextra -DDEBUG"
        EXTRA_CFLAGS "-I${CMAKE_CURRENT_SOURCE_DIR} -DSCULL_DEBUG" # include headers in cwd
)



