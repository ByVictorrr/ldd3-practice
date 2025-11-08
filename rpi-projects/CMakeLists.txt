cmake_minimum_required(VERSION 3.16)

project(rpi_kmods NONE)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON) # kernel prefers gnu11

# -----------------------------------------------------------------------------#
# Options
# -----------------------------------------------------------------------------#
option(IDE_ONLY "Allow configure to succeed for IDE indexing even if headers are incomplete" OFF)
option(KMOD_COPY_SOURCES "Copy sources instead of creating symlinks" ON)
option(KMOD_VERBOSE "Enable verbose kbuild output" OFF)
set(KMOD_STAGE_ROOT "${CMAKE_BINARY_DIR}/kmods" CACHE PATH "Where kmod sources are staged")
set(KMOD_GLOBAL_EXTRA_CFLAGS "" CACHE STRING "Global extra C flags for all kmods")

# -----------------------------------------------------------------------------#
# Host kernel + arch
# -----------------------------------------------------------------------------#
execute_process(COMMAND uname -r OUTPUT_VARIABLE KREL OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND uname -m OUTPUT_VARIABLE KMACH OUTPUT_STRIP_TRAILING_WHITESPACE)

if (KMACH MATCHES "^(aarch64|arm64)$")
    set(KARCH arm64)
elseif (KMACH MATCHES "^(armv6|armv7|arm)$")
    set(KARCH arm)
else()
    set(KARCH x86)
endif()

# -----------------------------------------------------------------------------#
# Kernel directories (KDIR = running kernel build, KSRC = headers, prefer ~/rpi-linux)
# -----------------------------------------------------------------------------#
set(_HOME_RPI_LINUX "$ENV{HOME}/rpi-linux")
execute_process(COMMAND uname -r
        OUTPUT_VARIABLE KREL
        OUTPUT_STRIP_TRAILING_WHITESPACE)

set(KDIR "/lib/modules/${KREL}/build" CACHE PATH "Kernel build directory (kbuild; includes include/generated/*)")
set(KSRC "${_HOME_RPI_LINUX}" CACHE PATH "Kernel *source* directory (includes include/uapi/*)")

# 1) KDIR: default to running kernel's build tree
if (NOT KDIR)
    set(KDIR "/lib/modules/${KREL}/build" CACHE PATH "" FORCE)
endif()

# 2) KSRC: prefer ~/rpi-linux for headers, else KDIR/source, else KDIR
if (NOT KSRC)
    if (EXISTS "${_HOME_RPI_LINUX}/include/uapi/linux/types.h")
        set(KSRC "${_HOME_RPI_LINUX}" CACHE PATH "" FORCE)
    elseif (EXISTS "${KDIR}/source/include/uapi/linux/types.h")
        set(KSRC "${KDIR}/source" CACHE PATH "" FORCE)
    else()
        set(KSRC "${KDIR}" CACHE PATH "" FORCE)
    endif()
endif()

# If KSRC still doesn't contain UAPI, probe Pi split-headers (*-rpt-common-rpi)
if (NOT EXISTS "${KSRC}/include/uapi/linux/types.h")
    file(GLOB _RPI_COMMON_CAND LIST_DIRECTORIES TRUE
            "/usr/src/linux-headers-*common-rpi"
            "/usr/src/linux-headers-*-rpt-common-rpi"
            "/usr/src/rpi-linux-headers-*common*")
    foreach(_c ${_RPI_COMMON_CAND})
        if (EXISTS "${_c}/include/uapi/linux/types.h")
            set(KSRC "${_c}" CACHE PATH "" FORCE)
            break()
        endif()
    endforeach()
endif()

# Sanity check against KDIR (real build tree)
set(_autoconf "${KDIR}/include/generated/autoconf.h")
if (NOT EXISTS "${_autoconf}")
    if (IDE_ONLY)
        message(WARNING
                "Missing ${_autoconf}. Proceeding due to IDE_ONLY=ON (indexing-only).\n"
                "If using a full kernel tree, run:\n"
                "  make -C ${KDIR} olddefconfig && make -C ${KDIR} prepare modules_prepare")
    else()
        message(FATAL_ERROR
                "Kernel headers at ${KDIR} look incomplete (missing include/generated/autoconf.h).\n"
                "If using a full kernel tree, run:\n"
                "  make -C ${KDIR} olddefconfig && make -C ${KDIR} prepare modules_prepare\n"
                "Current KDIR: ${KDIR}")
    endif()
endif()

if (NOT EXISTS "${KDIR}/Module.symvers")
    message(WARNING
            "Module.symvers not found in ${KDIR}. modpost may warn or fail.")
endif()

message(STATUS "Using KDIR (build):   ${KDIR}")
message(STATUS "Using KSRC (headers): ${KSRC}")

# -----------------------------------------------------------------------------#
# Kernel include directories (for IDE parsing too)
# -----------------------------------------------------------------------------#
set(_KINC
        ${KDIR}/include/generated
        ${KDIR}/include/generated/uapi
        ${KSRC}/include
        ${KSRC}/include/uapi
)

if (KARCH STREQUAL "arm64")
    list(APPEND _KINC
            ${KSRC}/arch/arm64/include
            ${KSRC}/arch/arm64/include/uapi
            ${KDIR}/arch/arm64/include
            ${KDIR}/arch/arm64/include/uapi
            ${KDIR}/arch/arm64/include/generated
            ${KDIR}/arch/arm64/include/generated/uapi
    )
elseif (KARCH STREQUAL "arm")
    list(APPEND _KINC
            ${KSRC}/arch/arm/include
            ${KSRC}/arch/arm/include/uapi
            ${KDIR}/arch/arm/include
            ${KDIR}/arch/arm/include/uapi
            ${KDIR}/arch/arm/include/generated
            ${KDIR}/arch/arm/include/generated/uapi
    )
endif()

include_directories(SYSTEM ${_KINC})

add_custom_target(headers
        COMMAND ${CMAKE_COMMAND} -E echo "KREL=${KREL}"
        COMMAND ${CMAKE_COMMAND} -E echo "KARCH=${KARCH}"
        COMMAND ${CMAKE_COMMAND} -E echo "KDIR=${KDIR}"
        COMMAND ${CMAKE_COMMAND} -E echo "KSRC=${KSRC}"
        COMMAND ${CMAKE_COMMAND} -E echo "KINC (search order):"
        COMMAND ${CMAKE_COMMAND} -E echo "${_KINC}"
)

# -----------------------------------------------------------------------------#
# Public API: kmod_target(NAME ... )
# -----------------------------------------------------------------------------#
function(kmod_target NAME)
    set(oneValueArgs EXTRA_CFLAGS MODULE_PARAMS)
    set(multiValueArgs SOURCES HEADERS)
    cmake_parse_arguments(KMOD "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (NOT KMOD_SOURCES)
        message(FATAL_ERROR "kmod_target(${NAME}) requires SOURCES")
    endif()

    set(STAGE "${KMOD_STAGE_ROOT}/${NAME}")
    file(MAKE_DIRECTORY "${STAGE}")

    set(OBJS "")
    set(INCDIRS "")
    set(STAGED_SRCS "")

    # Stage sources (copy or symlink) and collect object names
    foreach(src ${KMOD_SOURCES})
        get_filename_component(SRC_ABS "${src}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        get_filename_component(BASE "${SRC_ABS}" NAME)
        set(STAGED_SRC "${STAGE}/${BASE}")

        if (KMOD_COPY_SOURCES)
            add_custom_command(
                    OUTPUT "${STAGED_SRC}"
                    COMMAND ${CMAKE_COMMAND} -E make_directory "${STAGE}"
                    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${SRC_ABS}" "${STAGED_SRC}"
                    DEPENDS "${SRC_ABS}"
                    COMMENT "Staging source ${SRC_ABS} -> ${STAGED_SRC}"
                    VERBATIM
            )
        else()
            file(MAKE_DIRECTORY "${STAGE}")
            if (EXISTS "${STAGED_SRC}")
                file(REMOVE "${STAGED_SRC}")
            endif()
            file(CREATE_LINK "${SRC_ABS}" "${STAGED_SRC}" SYMBOLIC)
        endif()

        list(APPEND STAGED_SRCS "${STAGED_SRC}")

        string(REGEX REPLACE "\\.(c|S)$" ".o" OBJ "${BASE}")
        list(APPEND OBJS "${OBJ}")
        get_filename_component(SDIR "${SRC_ABS}" DIRECTORY)
        list(APPEND INCDIRS "${SDIR}")
    endforeach()

    # Stage headers similarly (if copying)
    foreach(h ${KMOD_HEADERS})
        get_filename_component(H_ABS "${h}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        if (EXISTS "${H_ABS}")
            get_filename_component(HBASE "${H_ABS}" NAME)
            set(STAGED_HDR "${STAGE}/${HBASE}")

            if (KMOD_COPY_SOURCES)
                add_custom_command(
                        OUTPUT "${STAGED_HDR}"
                        COMMAND ${CMAKE_COMMAND} -E make_directory "${STAGE}"
                        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${H_ABS}" "${STAGED_HDR}"
                        DEPENDS "${H_ABS}"
                        COMMENT "Staging header ${H_ABS} -> ${STAGED_HDR}"
                        VERBATIM
                )
                list(APPEND STAGED_SRCS "${STAGED_HDR}")
            else()
                file(MAKE_DIRECTORY "${STAGE}")
                if (EXISTS "${STAGED_HDR}")
                    file(REMOVE "${STAGED_HDR}")
                endif()
                file(CREATE_LINK "${H_ABS}" "${STAGED_HDR}" SYMBOLIC)
            endif()

            get_filename_component(HDIR "${H_ABS}" DIRECTORY)
            list(APPEND INCDIRS "${HDIR}")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES INCDIRS)

    # kbuild Makefile for this module
    set(MK "obj-m += ${NAME}.o\n")
    set(REST_OBJS "${OBJS}")
    list(REMOVE_ITEM REST_OBJS "${NAME}.o")
    if (REST_OBJS)
        string(REPLACE ";" " " REST_OBJS "${REST_OBJS}")
        string(APPEND MK "${NAME}-objs := ${REST_OBJS}\n")
    endif()

    # Compose EXTRA_CFLAGS
    set(INCFLAGS "")
    foreach(d ${INCDIRS})
        string(APPEND INCFLAGS " -I${d}")
    endforeach()

    set(_extra "")
    if (KMOD_GLOBAL_EXTRA_CFLAGS)
        string(APPEND _extra " ${KMOD_GLOBAL_EXTRA_CFLAGS}")
    endif()
    if (KMOD_EXTRA_CFLAGS)
        string(APPEND _extra " ${KMOD_EXTRA_CFLAGS}")
    endif()

    if (_extra STREQUAL "")
        string(APPEND MK "EXTRA_CFLAGS +=${INCFLAGS}\n")
    else()
        string(APPEND MK "EXTRA_CFLAGS +=${_extra}${INCFLAGS}\n")
    endif()

    file(WRITE "${STAGE}/Makefile" "${MK}")

    # kbuild invocation
    find_program(MAKE_CMD NAMES make gmake REQUIRED)
    set(_vflag "")
    if (KMOD_VERBOSE)
        set(_vflag "V=1")
    endif()

    add_custom_command(
            OUTPUT "${STAGE}/${NAME}.ko"
            COMMAND "${MAKE_CMD}" -C "${KDIR}" M=${STAGE} ${_vflag} modules
            DEPENDS ${STAGED_SRCS} "${STAGE}/Makefile"
            WORKING_DIRECTORY "${STAGE}"
            VERBATIM
    )
    add_custom_target(${NAME} DEPENDS "${STAGE}/${NAME}.ko")

    # IDE helper (never builds; feeds compile_commands.json)
    set(_ide_srcs "")
    foreach(_s ${KMOD_SOURCES} ${KMOD_HEADERS})
        get_filename_component(_abs "${_s}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        list(APPEND _ide_srcs "${_abs}")
    endforeach()
    add_library(${NAME}__ideobj OBJECT ${_ide_srcs})
    set_target_properties(${NAME}__ideobj PROPERTIES EXCLUDE_FROM_ALL YES)

    target_include_directories(${NAME}__ideobj SYSTEM PRIVATE ${INCDIRS} ${_KINC})
    target_compile_definitions(${NAME}__ideobj PRIVATE
            __KERNEL__ MODULE
            $<$<STREQUAL:${KARCH},arm64>:__aarch64__>
    )
    target_compile_options(${NAME}__ideobj PRIVATE
            -std=gnu11
            -nostdinc
            -Wno-declaration-after-statement
            -Wno-unused-parameter
            -Wno-sign-compare
            -Wno-missing-field-initializers
    )

    add_custom_target(${NAME}-ide SOURCES ${_ide_srcs})

    # Convenience targets
    set(_params "")
    if (KMOD_MODULE_PARAMS)
        set(_params "${KMOD_MODULE_PARAMS}")
    endif()

    add_custom_target(${NAME}-insmod
            DEPENDS ${NAME}
            COMMAND sudo insmod "${STAGE}/${NAME}.ko" ${_params} || (dmesg | tail -n 200; false)
            USES_TERMINAL
    )
    add_custom_target(${NAME}-rmmod
            COMMAND sudo rmmod -f ${NAME} || true
            USES_TERMINAL
    )
    add_custom_target(${NAME}-dmesg
            COMMAND dmesg | tail -n 200
            USES_TERMINAL
    )
    add_custom_target(${NAME}-clean
            COMMAND "${MAKE_CMD}" -C "${KDIR}" M=${STAGE} clean
            WORKING_DIRECTORY "${STAGE}"
            USES_TERMINAL
    )
endfunction()

# -----------------------------------------------------------------------------#
# Modules
# -----------------------------------------------------------------------------#
add_subdirectory(adc)
add_subdirectory(dac)
