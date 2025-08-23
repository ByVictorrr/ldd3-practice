cmake_minimum_required(VERSION 3.21)
project(mymod C)

set(CMAKE_C_STANDARD 99)
add_definitions(-D__KERNEL__ -DMODULE)

# Find running kernel release
execute_process(
        COMMAND uname -r
        OUTPUT_VARIABLE KERNEL_RELEASE
        OUTPUT_STRIP_TRAILING_WHITESPACE
)

set(KDIR "/usr/src/linux-headers-${KERNEL_RELEASE}")
set(ARCH "x86")

# Check that the headers are installed
if (NOT EXISTS "${KDIR}/include/linux/module.h")
    message(FATAL_ERROR "Kernel headers dir not found: ${KDIR}")
endif()

# Check that the headers are for the specified arch
if (NOT EXISTS "${KDIR}/arch/${ARCH}/include")
    message(FATAL_ERROR "Arch-specific include dir not found: ${KDIR}/arch/${ARCH}/include")
endif()

message(NOTICE "Building for arch: ${ARCH}")
message(NOTICE "Kernel release: ${KERNEL_RELEASE}")
message(NOTICE "Kernel headers: ${KDIR}")

# Include generic kernel headers
include_directories("${KDIR}/include")
include_directories("${KDIR}/include/generated")

# Include arch-specific kernel headers
include_directories("${KDIR}/arch/${ARCH}/include")
include_directories("${KDIR}/arch/${ARCH}/include/generated")

# Set some useful variables
set(MODULE_NAME scull)
set(SRC_FILE "${MODULE_NAME}.c")
set(OBJ_FILE "${MODULE_NAME}.o")
set(KO_FILE "${MODULE_NAME}.ko")

# Generate Kbuild file in the source directory
FILE(WRITE ${CMAKE_CURRENT_SOURCE_DIR}/Kbuild "obj-m := ${OBJ_FILE}")

# Custom `make` command used to build the module
add_custom_command(
        OUTPUT ${KO_FILE}
        COMMAND $(MAKE) -C ${KDIR} modules ARCH=${ARCH} M=${CMAKE_CURRENT_BINARY_DIR} src=${CMAKE_CURRENT_SOURCE_DIR}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        DEPENDS ${SRC_FILE}
        VERBATIM
)

add_custom_target(mymod ALL DEPENDS ${KO_FILE})
add_library(dummy_target "${SRC_FILE}")