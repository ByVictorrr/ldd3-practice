cmake_minimum_required(VERSION 3.21)
project(mmap_dma NONE)

set(COMMON_SCULL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../common_scull")
set(COMMON_EDU_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../common_edu")

kmod_target(pci_edu_mmap
        SOURCES
        main.c
        ${COMMON_EDU_DIR}/edu_core.c
        HEADERS
        edu_ioctl.h
        ${COMMON_EDU_DIR}/edu.h
        # EXTRA_CFLAGS "-Wno-error=format -g -fno-omit-frame-pointer" # include headers in cwd
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

