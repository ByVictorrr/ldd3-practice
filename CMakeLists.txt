cmake_minimum_required(VERSION 3.16)
# IMPORTANT: do NOT enable C; the kernel build system handles compilation.
project(ldd3_practice C)

# Avoid try_compile linking steps entirely (extra safety in IDEs)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Path to the *running* kernel's build dir (headers + kbuild)
execute_process(COMMAND uname -r
        OUTPUT_VARIABLE KREL
        OUTPUT_STRIP_TRAILING_WHITESPACE)
set(KDIR "/lib/modules/${KREL}/build" CACHE PATH "Kernel build dir")

set(KHDRS
        ${KDIR}/include
        ${KDIR}/include/generated
        ${KDIR}/include/uapi
        ${KDIR}/include/generated/uapi
        ${KDIR}/arch/x86/include
        ${KDIR}/arch/x86/include/generated
        ${KDIR}/arch/x86/include/uapi
)
add_library(kernel_headers INTERFACE
        ldd3/ch04_debug/debug.h)
target_include_directories(kernel_headers SYSTEM INTERFACE
        ${KDIR}/include
        ${KDIR}/include/generated
        ${KDIR}/include/uapi
        ${KDIR}/include/generated/uapi
        ${KDIR}/arch/x86/include
        ${KDIR}/arch/x86/include/generated
        ${KDIR}/arch/x86/include/uapi
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

    target_link_libraries(${NAME}_index PRIVATE kernel_headers)
    target_compile_definitions(${NAME}_index PRIVATE __KERNEL__ MODULE KBUILD_MODNAME=\"${NAME}\")

    # Convenience targets
    add_custom_target(${NAME}-insmod
            COMMAND sudo /sbin/insmod "${STAGE}/${NAME}.ko" || true
            DEPENDS ${NAME})
    add_custom_target(${NAME}-rmmod
            COMMAND sudo /sbin/rmmod "${NAME}" || true)
    add_custom_target(${NAME}-dmesg
            COMMAND dmesg --color=always | tail -n 60)
    add_custom_target(${NAME}-install
            COMMAND sudo mkdir -p "/lib/modules/${KREL}/extra"
            COMMAND sudo /bin/cp -f "${STAGE}/${NAME}.ko" "/lib/modules/${KREL}/extra/"
            COMMAND sudo /sbin/depmod "${KREL}"
            DEPENDS ${NAME})
endfunction()


# Bring in subprojects (each chapter)
add_subdirectory(ldd3/ch02_hello)
add_subdirectory(ldd3/ch03_scull)
add_subdirectory(ldd3/ch04_debug)
add_subdirectory(ldd3/ch05_concurrency)

