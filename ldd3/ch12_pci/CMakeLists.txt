cmake_minimum_required(VERSION 3.21)
project(pci_driver NONE)
set(COMMON_EDU_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../common_edu")
kmod_target(pci_edu
        SOURCES
            pci_edu.c
            # ${COMMON_EDU_DIR}/edu.c
        HEADERS
            # ${COMMON_EDU_DIR}/edu.h
        EXTRA_CFLAGS "-Wno-error=format -g -fno-omit-frame-pointer" # include headers in cwd
)
