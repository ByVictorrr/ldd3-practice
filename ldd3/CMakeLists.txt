cmake_minimum_required(VERSION 3.16)
# IMPORTANT: do NOT enable C; the kernel build system handles compilation.
project(ldd3 C)

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
set(QEMU_SH "${CMAKE_CURRENT_SOURCE_DIR}/../toolchain/qemu.sh")
if (NOT EXISTS "${QEMU_SH}")
    message(FATAL_ERROR "qemu.sh not found at: ${QEMU_SH}")
endif()

# Idempotent: qemu.sh should return success if the guest is already up

add_custom_target(qemu-up
        # COMMAND bash -lc "pkill -9 qemu; nohup '${QEMU_SH}' >/tmp/qemu.log 2>&1 </dev/null &; sleep 3"
        COMMAND bash -lc "tmux kill-session -t ldd3-qemu || true"
        # COMMAND bash -lc " nohup '${QEMU_SH}' >/tmp/qemu.log 2>&1 </dev/null &"
        COMMAND tmux new-session -d -s ldd3-qemu "/home/victord/git/ldd3-practice/toolchain/qemu.sh > /tmp/qemu.log 2>&1"
        COMMAND bash -lc "sleep 3"
        VERBATIM
        COMMENT "Start QEMU in background (non-blocking)"
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
    set(oneValueArgs EXTRA_CFLAGS MODULE_PARAMS)
    set(multiValueArgs SOURCES HEADERS)
    cmake_parse_arguments(KMOD "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
    if(NOT KMOD_SOURCES)
        message(FATAL_ERROR "kmod_target(${NAME}) requires SOURCES")
    endif()

    set(STAGE "${CMAKE_BINARY_DIR}/kmods/${NAME}")
    file(MAKE_DIRECTORY "${STAGE}")

    # --- Stage sources (absolute or relative); collect dirs for -I ---
    set(COMPONENT_OBJS)
    set(_seen_dirs "")
    set(_src_abspaths "")
    foreach(src ${KMOD_SOURCES})
        if(NOT IS_ABSOLUTE "${src}")
            set(_src_abs "${CMAKE_CURRENT_SOURCE_DIR}/${src}")
        else()
            set(_src_abs "${src}")
        endif()
        if(NOT EXISTS "${_src_abs}")
            message(FATAL_ERROR "Source not found: ${_src_abs}")
        endif()
        list(APPEND _src_abspaths "${_src_abs}")
        get_filename_component(_base "${_src_abs}" NAME)
        file(CREATE_LINK "${_src_abs}" "${STAGE}/${_base}" SYMBOLIC)
        if(_base MATCHES "\\.(c|S)$")
            string(REGEX REPLACE "\\.(c|S)$" ".o" _obj "${_base}")
            list(APPEND COMPONENT_OBJS "${_obj}")
        else()
            message(FATAL_ERROR "Unsupported source: ${_base} (expected .c or .S)")
        endif()
        get_filename_component(_dir "${_src_abs}" DIRECTORY)
        list(APPEND _seen_dirs "${_dir}")
    endforeach()

    # --- Stage headers passed explicitly; collect dirs too ---
    set(_hdr_abspaths "")
    foreach(hdr ${KMOD_HEADERS})
        if(NOT IS_ABSOLUTE "${hdr}")
            set(_hdr_abs "${CMAKE_CURRENT_SOURCE_DIR}/${hdr}")
        else()
            set(_hdr_abs "${hdr}")
        endif()
        if(EXISTS "${_hdr_abs}")
            list(APPEND _hdr_abspaths "${_hdr_abs}")
            get_filename_component(_hbase "${_hdr_abs}" NAME)
            file(CREATE_LINK "${_hdr_abs}" "${STAGE}/${_hbase}" SYMBOLIC)
            get_filename_component(_hdir "${_hdr_abs}" DIRECTORY)
            list(APPEND _seen_dirs "${_hdir}")
        endif()
    endforeach()

    # --- OPTIONAL: auto-stage quoted includes if HEADERS wasn’t provided ---
    if(NOT KMOD_HEADERS)
        foreach(_src_abs ${_src_abspaths})
            file(READ "${_src_abs}" _src_text)
            # find lines like: #include "foo.h"
            string(REGEX MATCHALL "#[ \t]*include[ \t]*\"([^\"]+)\"" _incs "${_src_text}")
            foreach(_line ${_incs})
                string(REGEX REPLACE ".*\"([^\"]+)\".*" "\\1" _inc_name "${_line}")
                foreach(_d ${_seen_dirs})
                    if(EXISTS "${_d}/${_inc_name}")
                        if(NOT EXISTS "${STAGE}/${_inc_name}")
                            file(CREATE_LINK "${_d}/${_inc_name}" "${STAGE}/${_inc_name}" SYMBOLIC)
                        endif()
                        break()
                    endif()
                endforeach()
            endforeach()
        endforeach()
    endif()

    # --- Kbuild Makefile: obj-m / -objs ---
    set(KBUILD_CONTENT "obj-m += ${NAME}.o\n")
    string(REPLACE ";" " " COMPONENT_OBJS_JOINED "${COMPONENT_OBJS}")
    if(NOT (KMOD_SOURCES STREQUAL "${NAME}.c" AND "${COMPONENT_OBJS_JOINED}" STREQUAL "${NAME}.o"))
        list(REMOVE_ITEM COMPONENT_OBJS "${NAME}.o")
        string(REPLACE ";" " " COMPONENT_OBJS_JOINED "${COMPONENT_OBJS}")
        if(NOT "${COMPONENT_OBJS_JOINED}" STREQUAL "")
            string(APPEND KBUILD_CONTENT "${NAME}-objs := ${COMPONENT_OBJS_JOINED}\n")
        endif()
    endif()

    # --- Compose EXTRA_CFLAGS: add -I for every unique dir we saw ---
    list(REMOVE_DUPLICATES _seen_dirs)
    set(_src_include_flags "")
    foreach(_d ${_seen_dirs})
        string(APPEND _src_include_flags " -I${_d}")
    endforeach()
    if(KMOD_EXTRA_CFLAGS)
        string(APPEND KBUILD_CONTENT "EXTRA_CFLAGS += ${KMOD_EXTRA_CFLAGS}${_src_include_flags}\n")
    else()
        string(APPEND KBUILD_CONTENT "EXTRA_CFLAGS +=${_src_include_flags}\n")
    endif()
    file(WRITE "${STAGE}/Makefile" "${KBUILD_CONTENT}")

    # --- Build the module with kbuild ---
    find_program(MAKE_EXECUTABLE NAMES make gmake REQUIRED)
    add_custom_command(
            OUTPUT "${STAGE}/${NAME}.ko"
            COMMAND "${MAKE_EXECUTABLE}" -C "${KDIR}" M=${STAGE} modules
            DEPENDS ${KMOD_SOURCES} ${KMOD_HEADERS} "${STAGE}/Makefile"
            WORKING_DIRECTORY "${STAGE}"
            VERBATIM
    )
    add_custom_target(${NAME} ALL DEPENDS "${STAGE}/${NAME}.ko")

    # --- CLion indexing target sees the same include dirs ---
    add_library(${NAME}_index STATIC EXCLUDE_FROM_ALL ${KMOD_SOURCES} ${KMOD_HEADERS})
    target_include_directories(${NAME}_index PRIVATE ${_seen_dirs})
    target_compile_definitions(${NAME}_index PRIVATE __KERNEL__ MODULE KBUILD_MODNAME=\"${NAME}\")

    # --- Convenience targets (unchanged, shortened here) ---
    set(_module_params "")
    if(KMOD_MODULE_PARAMS)
       set(_module_params "${KMOD_MODULE_PARAMS}")
    elseif(DEFINED ${NAME}_MODULE_PARAMS)
       # variable-of-variable expansion
       set(_module_params "${${NAME}_MODULE_PARAMS}")
    elseif(DEFINED KMOD_GLOBAL_MODULE_PARAMS)
       set(_module_params "${KMOD_GLOBAL_MODULE_PARAMS}")
    endif()

    message(STATUS "kmod_target(${NAME}): MODULE_PARAMS='${KMOD_MODULE_PARAMS}' -> effective '${_module_params}'")

    add_custom_target(${NAME}-insmod
            DEPENDS qemu-up ${NAME}
            COMMAND ${SCP_BIN} ${SCP_ARGS} "${STAGE}/${NAME}.ko" root@localhost:/tmp/${NAME}.ko
            COMMAND ${SSH_BIN} ${SSH_ARGS} "insmod /tmp/${NAME}.ko ${_module_params} || dmesg | tail -n 200"
            USES_TERMINAL
            VERBATIM
            COMMENT "insmod ${NAME} on guest"
    )

    add_custom_target(${NAME}-rmmod
            DEPENDS qemu-up                        # ← ensure QEMU first
            COMMAND ${SSH_BIN} ${SSH_ARGS} "rmmod -f ${NAME} || true"
            USES_TERMINAL
            VERBATIM
            COMMENT "rmmod ${NAME} on guest"
    )

    add_custom_target(${NAME}-dmesg
            DEPENDS qemu-up
            COMMAND ${SSH_BIN} ${SSH_ARGS} "sudo dmesg | tail -n 200"
            USES_TERMINAL
            VERBATIM
            COMMENT "kernel messages (guest)"
    )

    add_custom_target(${NAME}-gdb-break
            DEPENDS ${NAME}-insmod
            COMMAND ${SSH_BIN} ${SSH_ARGS} "echo g > /proc/sysrq-trigger; sleep 2; echo g > /proc/sysrq-trigger"
            # COMMAND ${SSH_BIN} ${SSH_ARGS} "echo g > /proc/sysrq-trigger"
            USES_TERMINAL
            VERBATIM
            COMMENT "insmod ${NAME} then break into KGDB"
    )

endfunction()

# Bring in subprojects (each chapter)
add_subdirectory(ch02_hello)
add_subdirectory(ch03_scull)
add_subdirectory(ch04_debug)
add_subdirectory(ch05_concurrency)
add_subdirectory(ch06_advanced_char)
add_subdirectory(ch07_timers)
add_subdirectory(ch08_memory)
add_subdirectory(ch09_hw_communication)
add_subdirectory(ch10_interrupts)
add_subdirectory(ch11_ktypes)

