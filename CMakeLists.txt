cmake_minimum_required(VERSION 3.16)
# IMPORTANT: do NOT enable C; the kernel build system handles compilation.
project(ldd3_practice C)

# Avoid try_compile linking steps entirely (extra safety in IDEs)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(SSH_BIN ssh)
set(SSH_ARGS
        -o UserKnownHostsFile=/dev/null
        -o StrictHostKeyChecking=no
        -p 2222
        root@localhost
)

set(SCP_BIN scp)
set(SCP_ARGS
        -o UserKnownHostsFile=/dev/null
        -o StrictHostKeyChecking=no
        -P 2222
        -O           # force legacy scp protocol (Dropbear-friendly)
)
# Path to the *running* kernel's build dir (headers + kbuild)
execute_process(COMMAND uname -r
        OUTPUT_VARIABLE KREL
        OUTPUT_STRIP_TRAILING_WHITESPACE)
set(KREL 6.16.0)
set(KARCH x86)

set(KDIR "/lib/modules/${KREL}/build" CACHE PATH "Kernel build dir")

set(KHDRS
        ${KDIR}/include
        ${KDIR}/include/generated
        ${KDIR}/include/uapi
        ${KDIR}/include/generated/uapi
        ${KDIR}/arch/${KARCH}/include
        ${KDIR}/arch/${KARCH}/include/generated
        ${KDIR}/arch/${KARCH}/include/uapi
        ${KDIR}/arch/${KARCH}/include/generated/uapi
)
include_directories(
        ${KDIR}/include
        ${KDIR}/include/generated
        ${KDIR}/include/uapi
        ${KDIR}/include/generated/uapi
        ${KDIR}/arch/${KARCH}/include
        ${KDIR}/arch/${KARCH}/include/generated
        ${KDIR}/arch/${KARCH}/include/uapi
        ${KDIR}/arch/${KARCH}/include/generated/uapi
)
if (NOT EXISTS "${KDIR}/Makefile")
    message(FATAL_ERROR "Kernel build dir not found: ${KDIR}. Install the matching linux-headers for your kernel.")
endif()

# usage:
# kmod_target(<name> SOURCES file1.c [file2.c ...]
#             [HEADERS file1.h ...]
#             [EXTRA_CFLAGS "…"])
function(kmod_target NAME)
    set(options)
    set(oneValueArgs EXTRA_CFLAGS)
    set(multiValueArgs SOURCES HEADERS)
    cmake_parse_arguments(KMOD "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT KMOD_SOURCES)
        message(FATAL_ERROR "kmod_target(${NAME}) requires SOURCES")
    endif()

    set(STAGE "${CMAKE_BINARY_DIR}/kmods/${NAME}")
    file(MAKE_DIRECTORY "${STAGE}")

    # Symlink sources (and headers if given) into the staging dir
    set(COMPONENT_OBJS)
    foreach(src ${KMOD_SOURCES})
        get_filename_component(base "${src}" NAME)
        file(CREATE_LINK "${CMAKE_CURRENT_SOURCE_DIR}/${src}" "${STAGE}/${base}" SYMBOLIC)
        if(base MATCHES "\\.(c|S)$")
            string(REGEX REPLACE "\\.(c|S)$" ".o" obj "${base}")
            list(APPEND COMPONENT_OBJS "${obj}")
        else()
            message(FATAL_ERROR "Unsupported source: ${base} (expected .c or .S)")
        endif()
    endforeach()

    foreach(hdr ${KMOD_HEADERS})
        get_filename_component(hbase "${hdr}" NAME)
        if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/${hdr}")
            file(CREATE_LINK "${CMAKE_CURRENT_SOURCE_DIR}/${hdr}" "${STAGE}/${hbase}" SYMBOLIC)
        endif()
    endforeach()

    # If single-source ${NAME}.c, don't emit ${NAME}-objs
    set(KBUILD_CONTENT "obj-m += ${NAME}.o\n")
    string(REPLACE ";" " " COMPONENT_OBJS_JOINED "${COMPONENT_OBJS}")
    if(NOT (KMOD_SOURCES STREQUAL "${NAME}.c" AND "${COMPONENT_OBJS_JOINED}" STREQUAL "${NAME}.o"))
        list(REMOVE_ITEM COMPONENT_OBJS "${NAME}.o")
        string(REPLACE ";" " " COMPONENT_OBJS_JOINED "${COMPONENT_OBJS}")
        if(NOT "${COMPONENT_OBJS_JOINED}" STREQUAL "")
            string(APPEND KBUILD_CONTENT "${NAME}-objs := ${COMPONENT_OBJS_JOINED}\n")
        endif()
    endif()

    # Add -I for each unique source dir (handy for #include "local.h")
    set(_seen_dirs "")
    set(_src_include_flags "")
    foreach(src ${KMOD_SOURCES} ${KMOD_HEADERS})
        get_filename_component(_abs "${CMAKE_CURRENT_SOURCE_DIR}/${src}" ABSOLUTE)
        get_filename_component(_dir "${_abs}" DIRECTORY)
        list(FIND _seen_dirs "${_dir}" _idx)
        if(_idx EQUAL -1)
            list(APPEND _seen_dirs "${_dir}")
            string(APPEND _src_include_flags " -I${_dir}")
        endif()
    endforeach()

    if(KMOD_EXTRA_CFLAGS)
        string(APPEND KBUILD_CONTENT "EXTRA_CFLAGS += ${KMOD_EXTRA_CFLAGS}${_src_include_flags}\n")
    else()
        string(APPEND KBUILD_CONTENT "EXTRA_CFLAGS +=${_src_include_flags}\n")
    endif()

    file(WRITE "${STAGE}/Makefile" "${KBUILD_CONTENT}")

    # Always use GNU make for Kbuild
    find_program(MAKE_EXECUTABLE NAMES make gmake REQUIRED)

    add_custom_command(
            OUTPUT "${STAGE}/${NAME}.ko"
            COMMAND "${MAKE_EXECUTABLE}" -C "${KDIR}" M=${STAGE} modules
            DEPENDS ${KMOD_SOURCES} ${KMOD_HEADERS} "${STAGE}/Makefile"
            WORKING_DIRECTORY "${STAGE}"
            VERBATIM
    )
    add_custom_target(${NAME} ALL DEPENDS "${STAGE}/${NAME}.ko")

    # ---- CLion indexing-only dummy (no compile/link) ----
    add_library(${NAME}_index STATIC EXCLUDE_FROM_ALL ${KMOD_SOURCES} ${KMOD_HEADERS})
    get_filename_component(_mod_srcdir "${CMAKE_CURRENT_SOURCE_DIR}" ABSOLUTE)
    target_include_directories(${NAME}_index PRIVATE "${_mod_srcdir}")

    target_link_libraries(${NAME}_index PRIVATE KH)
    target_compile_definitions(${NAME}_index PRIVATE __KERNEL__ MODULE KBUILD_MODNAME=\"${NAME}\")

    # Convenience targets
    # Fixed ssh/scp commands to the QEMU guest
    # insmod: push to /tmp, then insmod (print dmesg on failure)
    # insmod: push .ko, then insmod in guest; on failure, show dmesg and fail
    # insmod: push .ko, then insmod in guest; on failure, show dmesg and fail
    # insmod: push .ko, then insmod; on failure, try to print logs and fail
    # insmod: push, try to load; on failure print logs and FAIL the target
    # insmod: push, then load; on failure print logs and FAIL the target
    add_custom_target(${NAME}-insmod
            COMMAND ${SCP_BIN} ${SCP_ARGS} "${STAGE}/${NAME}.ko" root@localhost:/tmp/${NAME}.ko
            COMMAND ${SSH_BIN} ${SSH_ARGS}
            "insmod /tmp/${NAME}.ko || { (sudo dmesg || dmesg) | tail -n 200; false; }"
            DEPENDS ${NAME}
            USES_TERMINAL
            VERBATIM
            COMMENT "insmod ${NAME} on guest"
    )

    # rmmod: ignore if not present
    add_custom_target(${NAME}-rmmod
            COMMAND ${SSH_BIN} ${SSH_ARGS} "rmmod -f ${NAME} || true"
            USES_TERMINAL
            VERBATIM
            COMMENT "rmmod ${NAME} on guest"
    )

    # dmesg: use your working pipeline
    add_custom_target(${NAME}-dmesg
            COMMAND ${SSH_BIN} ${SSH_ARGS} "sudo dmesg | tail -n 200"
            USES_TERMINAL
            VERBATIM
            COMMENT "kernel messages (guest)"
    )

    # install into guest /lib/modules/$(uname -r)/extra and depmod
    # NOTE: use $$ so $ reaches the remote shell for expansion
    add_custom_target(${NAME}-install
            COMMAND ${SCP_BIN} ${SCP_ARGS} "${STAGE}/${NAME}.ko" root@localhost:/tmp/${NAME}.ko
            COMMAND ${SSH_BIN} ${SSH_ARGS}
            "uname_r=\$$(uname -r); mkdir -p /lib/modules/\$${uname_r}/extra && cp -f /tmp/${NAME}.ko /lib/modules/\$${uname_r}/extra/${NAME}.ko && depmod \$${uname_r} || true"
            DEPENDS ${NAME}
            USES_TERMINAL
            VERBATIM
            COMMENT "install ${NAME} into guest /lib/modules/.../extra"
    )
endfunction()
# Bring in subprojects (each chapter)
add_subdirectory(ldd3/ch02_hello)
add_subdirectory(ldd3/ch03_scull)
add_subdirectory(ldd3/ch04_debug)
add_subdirectory(ldd3/ch05_concurrency)
add_subdirectory(ldd3/ch06_advanced_char)
add_subdirectory(ldd3/ch07_timers)

