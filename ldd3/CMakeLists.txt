cmake_minimum_required(VERSION 3.16)

# We are building kernel modules; we don't use CMake's normal C linker path.
project(ldd3 C)

# Avoid try_compile linking steps entirely (extra safety in IDEs)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# ---------------------------------------------------------------------------
# QEMU / SSH helpers
# ---------------------------------------------------------------------------

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

# Start QEMU in a detached tmux session (non-blocking)
add_custom_target(qemu-up
        # COMMAND bash -lc "tmux kill-session -t ldd3-qemu || true"
        # COMMAND tmux new-session -d -s ldd3-qemu "${QEMU_SH} > /tmp/qemu.log 2>&1"
        # COMMAND bash -lc "sleep 10"
        VERBATIM
        COMMENT "Start QEMU in background (non-blocking)"
)

# ---------------------------------------------------------------------------
# Kernel build / headers setup
# ---------------------------------------------------------------------------

# Use running kernel's release as default, but override here if needed.
execute_process(COMMAND uname -r
        OUTPUT_VARIABLE KREL
        OUTPUT_STRIP_TRAILING_WHITESPACE)
# Override (you had this hardcoded)
set(KREL 6.16.0)
set(KARCH x86)

set(KDIR "/lib/modules/${KREL}/build" CACHE PATH "Kernel build dir")

if (NOT EXISTS "${KDIR}/Makefile")
    message(FATAL_ERROR "Kernel build dir not found: ${KDIR}. Install the matching linux-headers for your kernel.")
endif()

# Kernel include directories for CLion indexing / static analysis (not for kbuild)
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

include_directories(${KHDRS})

# ---------------------------------------------------------------------------
# kmod_target(NAME ...): build a kernel module via kbuild
#
# Usage:
#   kmod_target(mint_bus
#       SOURCES mint_bus.c mint_utils.c
#       HEADERS mint.h
#   )
#
#   kmod_target(mint_driver
#       SOURCES mint_driver.c
#       HEADERS mint.h
#       DEPENDS_MODS mint_bus         # <-- will wire KBUILD_EXTRA_SYMBOLS etc.
#   )
# ---------------------------------------------------------------------------
function(kmod_target NAME)
    set(options)
    set(oneValueArgs EXTRA_CFLAGS MODULE_PARAMS)
    set(multiValueArgs SOURCES HEADERS DEPENDS_MODS)
    cmake_parse_arguments(KMOD "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT KMOD_SOURCES)
        message(FATAL_ERROR "kmod_target(${NAME}) requires SOURCES")
    endif()

    # Stage directory for this module
    set(STAGE "${CMAKE_BINARY_DIR}/kmods/${NAME}")
    file(MAKE_DIRECTORY "${STAGE}")

    # --- Stage sources and collect object names + include dirs ---
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
    foreach(hdr ${KMOD_HEADERS})
        if(NOT IS_ABSOLUTE "${hdr}")
            set(_hdr_abs "${CMAKE_CURRENT_SOURCE_DIR}/${hdr}")
        else()
            set(_hdr_abs "${hdr}")
        endif()
        if(EXISTS "${_hdr_abs}")
            get_filename_component(_hbase "${_hdr_abs}" NAME)
            file(CREATE_LINK "${_hdr_abs}" "${STAGE}/${_hbase}" SYMBOLIC)
            get_filename_component(_hdir "${_hdr_abs}" DIRECTORY)
            list(APPEND _seen_dirs "${_hdir}")
        endif()
    endforeach()

    # --- Auto-stage quoted includes from sources if HEADERS wasn't given ---
    if(NOT KMOD_HEADERS)
        foreach(_src_abs ${_src_abspaths})
            file(READ "${_src_abs}" _src_text)
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

    # --- EXTRA_CFLAGS + per-source include dirs ---
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

    # --- KBUILD_EXTRA_SYMBOLS for inter-module symbol deps ---
    # If this module depends on symbols from other modules (DEPENDS_MODS),
    # add their Module.symvers paths so modpost can resolve exported symbols.
    foreach(dep_mod ${KMOD_DEPENDS_MODS})
        # Kbuild runs in ${STAGE}, other modules live in ../<dep_mod>/
        string(APPEND KBUILD_CONTENT
                "KBUILD_EXTRA_SYMBOLS += ../${dep_mod}/Module.symvers\n")
    endforeach()

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

    # If this module depends on others, enforce build order at CMake level too.
    foreach(dep_mod ${KMOD_DEPENDS_MODS})
        add_dependencies(${NAME} ${dep_mod})
    endforeach()

    # --- CLion indexing target (purely for IDE) ---
    add_library(${NAME}_index STATIC EXCLUDE_FROM_ALL ${KMOD_SOURCES} ${KMOD_HEADERS})
    target_include_directories(${NAME}_index PRIVATE ${_seen_dirs})
    target_compile_definitions(${NAME}_index PRIVATE __KERNEL__ MODULE KBUILD_MODNAME=\"${NAME}\")

    # --- Convenience targets for insmod/rmmod/dmesg/gdb ---
    set(_module_params "")
    if(KMOD_MODULE_PARAMS)
        set(_module_params "${KMOD_MODULE_PARAMS}")
    elseif(DEFINED ${NAME}_MODULE_PARAMS)
        set(_module_params "${${NAME}_MODULE_PARAMS}")
    elseif(DEFINED KMOD_GLOBAL_MODULE_PARAMS)
        set(_module_params "${KMOD_GLOBAL_MODULE_PARAMS}")
    endif()

    message(STATUS "kmod_target(${NAME}): MODULE_PARAMS='${KMOD_MODULE_PARAMS}' -> effective '${_module_params}'")

    # Build list of insmod targets for dependent modules
    set(_insmod_deps "")
    foreach(dep_mod ${KMOD_DEPENDS_MODS})
        list(APPEND _insmod_deps "${dep_mod}-insmod")
    endforeach()

    add_custom_target(${NAME}-insmod
            DEPENDS qemu-up ${NAME} ${_insmod_deps}
            COMMAND ${SCP_BIN} ${SCP_ARGS} "${STAGE}/${NAME}.ko" root@localhost:/tmp/${NAME}.ko
            COMMAND ${SSH_BIN} ${SSH_ARGS} "insmod /tmp/${NAME}.ko ${_module_params} || dmesg | tail -n 200"
            COMMAND ${SSH_BIN} ${SSH_ARGS} "echo 'module ${NAME} +p' > /sys/kernel/debug/dynamic_debug/control"
            USES_TERMINAL
            VERBATIM
            COMMENT "insmod ${NAME} on guest"
    )

    add_custom_target(${NAME}-rmmod
            DEPENDS qemu-up
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
            USES_TERMINAL
            VERBATIM
            COMMENT "insmod ${NAME} then break into KGDB"
    )

endfunction()

# ---------------------------------------------------------------------------
# Userspace helper binaries (unchanged, just cleaned up comments)
# ---------------------------------------------------------------------------
function(userprog_target NAME)
    set(options)
    set(oneValueArgs REMOTE_DIR REMOTE_NAME)
    set(multiValueArgs SOURCES INCLUDES LIBS RUN_CMDS)
    cmake_parse_arguments(USR "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (NOT USR_SOURCES)
        message(FATAL_ERROR "userprog_target(${NAME}) requires SOURCES")
    endif()

    if (NOT USR_REMOTE_DIR)
        set(USR_REMOTE_DIR "/usr/local/bin")
    endif()
    if (NOT USR_REMOTE_NAME)
        set(USR_REMOTE_NAME "${NAME}")
    endif()

    add_executable(${NAME} ${USR_SOURCES})

    # Nuke inherited kernel include dirs for this userspace target
    set_property(TARGET ${NAME} PROPERTY INCLUDE_DIRECTORIES "")

    if (USR_INCLUDES)
        target_include_directories(${NAME} PRIVATE ${USR_INCLUDES})
    endif()

    if (USR_LIBS)
        target_link_libraries(${NAME} PRIVATE ${USR_LIBS})
    endif()

    # Push kernel includes after system headers
    target_compile_options(${NAME} PRIVATE
            "$<$<COMPILE_LANGUAGE:C>:-idirafter${KDIR}/include>"
            "$<$<COMPILE_LANGUAGE:C>:-idirafter${KDIR}/include/generated>"
            "$<$<COMPILE_LANGUAGE:C>:-idirafter${KDIR}/include/uapi>"
            "$<$<COMPILE_LANGUAGE:C>:-idirafter${KDIR}/include/generated/uapi>"
            "$<$<COMPILE_LANGUAGE:C>:-idirafter${KDIR}/arch/${KARCH}/include>"
            "$<$<COMPILE_LANGUAGE:C>:-idirafter${KDIR}/arch/${KARCH}/include/generated>"
            "$<$<COMPILE_LANGUAGE:C>:-idirafter${KDIR}/arch/${KARCH}/include/uapi>"
            "$<$<COMPILE_LANGUAGE:C>:-idirafter${KDIR}/arch/${KARCH}/include/generated/uapi>"
            "$<$<COMPILE_LANGUAGE:C>:-idirafter${KDIR}/arch/${KARCH}/include/generated/uapi>"
    )

    add_custom_target(${NAME}-push
            DEPENDS qemu-up ${NAME}
            COMMAND ${SCP_BIN} ${SCP_ARGS} $<TARGET_FILE:${NAME}> root@localhost:${USR_REMOTE_DIR}/${USR_REMOTE_NAME}
            COMMAND ${SSH_BIN} ${SSH_ARGS} "chmod +x ${USR_REMOTE_DIR}/${USR_REMOTE_NAME}"
            USES_TERMINAL
            VERBATIM
            COMMENT "scp ${NAME} -> ${USR_REMOTE_DIR}/${USR_REMOTE_NAME} in guest"
    )

    # Build remote command string
    if (USR_RUN_CMDS)
        # Join multiple RUN_CMDS into: "cmd1 && cmd2 && cmd3"
        string(JOIN " && " _remote_cmd "${USR_RUN_CMDS}")
    else()
        # Default: just run the binary with optional ARGS
        set(_remote_cmd "${USR_REMOTE_DIR}/${USR_REMOTE_NAME} $<IF:$<BOOL:${ARGS}>,${ARGS},>")
    endif()

    add_custom_target(${NAME}-run
            DEPENDS ${NAME}-push
            COMMAND ${SSH_BIN} ${SSH_ARGS} "${_remote_cmd}"
            USES_TERMINAL
            VERBATIM
            COMMENT "run ${USR_REMOTE_NAME} inside guest"
    )
endfunction()

function(remote_scp_and_run NAME)
    set(options)
    set(oneValueArgs LOCAL_PATH REMOTE_PATH)
    set(multiValueArgs SSH_CMDS)
    cmake_parse_arguments(R "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (NOT R_LOCAL_PATH)
        message(FATAL_ERROR "remote_scp_and_run(${NAME}) requires LOCAL_PATH=<path-to-local-file>")
    endif()
    if (NOT R_REMOTE_PATH)
        message(FATAL_ERROR "remote_scp_and_run(${NAME}) requires REMOTE_PATH=<remote-path>")
    endif()
    if (NOT R_SSH_CMDS)
        message(FATAL_ERROR "remote_scp_and_run(${NAME}) requires SSH_CMDS")
    endif()

    # Join SSH commands with && so they run in one shell
    string(REPLACE ";" " && " SSH_CMD_JOINED "${R_SSH_CMDS}")

    add_custom_target(${NAME}
            DEPENDS qemu-up
            COMMAND ${SCP_BIN} ${SCP_ARGS}
            "${R_LOCAL_PATH}"
            root@localhost:"${R_REMOTE_PATH}"
            COMMAND ${SSH_BIN} ${SSH_ARGS}
            "${SSH_CMD_JOINED}"
            USES_TERMINAL
            VERBATIM
            COMMENT "scp ${R_LOCAL_PATH} to ${R_REMOTE_PATH} and run commands on guest"
    )
endfunction()


# ---------------------------------------------------------------------------
# Subdirectories for chapters
# ---------------------------------------------------------------------------
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
add_subdirectory(ch12_pci)
add_subdirectory(ch13_usb)
add_subdirectory(ch14_device_model)
add_subdirectory(ch15_mmap_dma)
add_subdirectory(ch16_block)
