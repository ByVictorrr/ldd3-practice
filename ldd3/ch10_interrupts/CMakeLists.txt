cmake_minimum_required(VERSION 3.21)
project(interrupts NONE)
kmod_target(shortirq_tasklet
        SOURCES shortirq.c
        MODULE_PARAMS "bh_mode=tasklet"
        # EXTRA_CFLAGS "-Wall -Wextra -DDEBUG"
        EXTRA_CFLAGS "-I${CMAKE_CURRENT_SOURCE_DIR} -DSCULL_DEBUG" # include headers in cwd
)
kmod_target(shortirq_threaded
        SOURCES shortirq.c
        MODULE_PARAMS "bh_mode=threaded"
        # EXTRA_CFLAGS "-Wall -Wextra -DDEBUG"
        EXTRA_CFLAGS "-I${CMAKE_CURRENT_SOURCE_DIR} -DSCULL_DEBUG" # include headers in cwd
)
kmod_target(shortirq_workqueue
        SOURCES shortirq.c
        MODULE_PARAMS "bh_mode=workqueue"
        # EXTRA_CFLAGS "-Wall -Wextra -DDEBUG"
        EXTRA_CFLAGS "-I${CMAKE_CURRENT_SOURCE_DIR} -DSCULL_DEBUG" # include headers in cwd
)


kmod_target(edu_irq
        SOURCES pci_eduirq.c
        # EXTRA_CFLAGS "-Wall -Wextra -DDEBUG"
        EXTRA_CFLAGS "-I${CMAKE_CURRENT_SOURCE_DIR} -DSCULL_DEBUG" # include headers in cwd
)

