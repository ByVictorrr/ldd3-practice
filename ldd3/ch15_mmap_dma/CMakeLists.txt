cmake_minimum_required(VERSION 3.21)
project(mmap_dma NONE)

set(COMMON_SCULL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../common_scull")

kmod_target(pci_edu_ext
        SOURCES edu.c
        HEADERS edu_ioctl.h
        # DEPENDS_MODS mint_class
)
set(REMOTE_BIN_DIR "/usr/local/bin")

userprog_target(edu_test
        SOURCES edu_test.c
        INCLUDES edu_ioctl.h
        REMOTE_DIR ${REMOTE_BIN_DIR}
        RUN_CMDS
        "${REMOTE_BIN_DIR}/edu_test 1"
        "${REMOTE_BIN_DIR}/edu_test 2"
        "${REMOTE_BIN_DIR}/edu_test 3"
        "${REMOTE_BIN_DIR}/edu_test 4"
)

kmod_target(scullvma
        SOURCES scullv.c
        HEADERS  ${COMMON_SCULL_DIR}/scull.h
        # EXTRA_CFLAGS "-Wall -Wextra -DDEBUG"
        EXTRA_CFLAGS "-I${CMAKE_CURRENT_SOURCE_DIR} -DSCULL_DEBUG" # include headers in cwd
)


userprog_target(scullv_test
        SOURCES scullv_test.c
        REMOTE_DIR ${REMOTE_BIN_DIR}
        RUN_CMDS
        "${REMOTE_BIN_DIR}/scullv_test"
)

