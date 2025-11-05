cmake_minimum_required(VERSION 3.16)

# -----------------------------------------------------------------------------
# Project & global config
# -----------------------------------------------------------------------------
project(rpi_kmods NONE)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_EXTENSIONS ON)     # kernel prefers gnu11

# -----------------------------------------------------------------------------
# Detect running kernel / arch (when configuring on the Pi)
# -----------------------------------------------------------------------------
execute_process(COMMAND uname -r OUTPUT_VARIABLE KREL OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND uname -m OUTPUT_VARIABLE KMACH OUTPUT_STRIP_TRAILING_WHITESPACE)

# Kernel arch mapping
if (KMACH MATCHES "^(aarch64|arm64)$")
    set(KARCH arm64)
elseif (KMACH MATCHES "^(armv6|armv7|arm)$")
    set(KARCH arm)
else()
    set(KARCH x86)  # fallback if configuring off-target
endif()

# -----------------------------------------------------------------------------
# Kernel directories
#   KDIR  -> /lib/modules/$(uname -r)/build       (kbuild "build" tree)
#   KSRC  -> kernel source tree that contains include/uapi/* (split packages)
# -----------------------------------------------------------------------------
set(KDIR "/lib/modules/${KREL}/build" CACHE PATH "Kernel build directory (kbuild)")
set(KSRC "" CACHE PATH "Kernel *source* directory (if split from KDIR)")

# --- Robust autodetect for Raspberry Pi split headers ------------------------
# If KSRC wasn't provided, try the 'source' symlink first...
if (NOT KSRC)
    if (EXISTS "${KDIR}/source/include/linux")
        set(KSRC "${KDIR}/source")
    else()
        set(KSRC "${KDIR}")
    endif()
endif()

# If KSRC doesn't actually contain UAPI, try to find a matching *-common-rpi tree
if (NOT EXISTS "${KSRC}/include/uapi/linux/types.h")
    # common patterns used by Raspberry Pi header packages
    file(GLOB _RPI_COMMON_CAND
            LIST_DIRECTORIES TRUE
            "/usr/src/linux-headers-*common-rpi"
            "/usr/src/linux-headers-*-rpt-common-rpi"
            "/usr/src/rpi-linux-headers-*common*"
    )
    set(_FOUND_COMMON FALSE)
    foreach(_c ${_RPI_COMMON_CAND})
        if (EXISTS "${_c}/include/uapi/linux/types.h")
            set(KSRC "${_c}")
            set(_FOUND_COMMON TRUE)
            break()
        endif()
    endforeach()
    if (NOT _FOUND_COMMON)
        message(WARNING
                "Could not locate UAPI headers (linux/types.h). "
                "KSRC currently '${KSRC}'. If you have a split header install, point KSRC to the "
                " '*-common-rpi' directory, e.g. -DKSRC=/usr/src/linux-headers-<ver>-rpt-common-rpi")
    endif()
endif()

# -----------------------------------------------------------------------------
# Early sanity checks so we fail fast with a helpful message
# -----------------------------------------------------------------------------
if (NOT EXISTS "${KDIR}/include/generated/autoconf.h")
    message(FATAL_ERROR
            "Kernel headers at ${KDIR} look incomplete or incorrect.\n"
            "Ensure /lib/modules/${KREL}/build points to packaged headers "
            "and contains include/generated/autoconf.h.\n"
            "Current KDIR: ${KDIR}\n"
    )
endif()

# Module.symvers helps modpost (not strictly required to compile)
if (NOT EXISTS "${KDIR}/Module.symvers")
    message(WARNING
            "Module.symvers not found in ${KDIR}. modpost may warn or fail.\n"
            "On Raspberry Pi headers this usually exists. If using a custom tree, "
            "build at least one module or install the packaged linux-headers-${KREL}.\n"
            "Current KDIR: ${KDIR}\n"
    )
endif()

# -----------------------------------------------------------------------------
# Options for staging & quality-of-life
# -----------------------------------------------------------------------------
set(KMOD_STAGE_ROOT "${CMAKE_BINARY_DIR}/kmods"
        CACHE PATH "Root directory where kmod sources are staged for kbuild")
option(KMOD_COPY_SOURCES "Copy sources instead of creating symlinks" ON)
set(KMOD_GLOBAL_EXTRA_CFLAGS "" CACHE STRING "Global extra C flags for all kmods")
option(KMOD_VERBOSE "Enable verbose kbuild output" OFF)

# -----------------------------------------------------------------------------
# Kernel include directories used by IDE helper targets (for parsing only)
#   Generated headers come from ${KDIR}/include/generated (+ arch/generated)
#   Source headers (linux/*, uapi/*, arch/*) come from ${KSRC}
# -----------------------------------------------------------------------------
set(_KINC
        ${KDIR}/include/generated
        ${KDIR}/include/generated/uapi
        ${KSRC}/include
        ${KSRC}/include/uapi
)

# Arch-specific source includes from KSRC
if (KARCH STREQUAL "arm64")
    list(APPEND _KINC
            ${KSRC}/arch/arm64/include
            ${KSRC}/arch/arm64/include/uapi
    )
elseif (KARCH STREQUAL "arm")
    list(APPEND _KINC
            ${KSRC}/arch/arm/include
            ${KSRC}/arch/arm/include/uapi
    )
endif()

# Arch-specific *generated* includes from KDIR (crucial for Pi headers)
if (KARCH STREQUAL "arm64")
    list(APPEND _KINC
            ${KDIR}/arch/arm64/include
            ${KDIR}/arch/arm64/include/uapi
            ${KDIR}/arch/arm64/include/generated
            ${KDIR}/arch/arm64/include/generated/uapi
    )
elseif (KARCH STREQUAL "arm")
    list(APPEND _KINC
            ${KDIR}/arch/arm/include
            ${KDIR}/arch/arm/include/uapi
            ${KDIR}/arch/arm/include/generated
            ${KDIR}/arch/arm/include/generated/uapi
    )
endif()

# (Optional) Also inject globally so standalone files in the IDE parse decently
include_directories(${_KINC})
add_custom_target(headers
        COMMAND ${CMAKE_COMMAND} -E echo "KDIR=${KDIR}"
        COMMAND ${CMAKE_COMMAND} -E echo "KSRC=${KSRC}"
        COMMAND ${CMAKE_COMMAND} -E echo "KARCH=${KARCH}"
        COMMAND ${CMAKE_COMMAND} -E echo "KINC (search order):"
        COMMAND ${CMAKE_COMMAND} -E echo "${_KINC}"
)

# -----------------------------------------------------------------------------
# Internal: helper to stage files (copy or symlink) into a per-target directory
# -----------------------------------------------------------------------------
function(_kmod_stage SRC DEST_DIR)
    file(MAKE_DIRECTORY "${DEST_DIR}")
    get_filename_component(_base "${SRC}" NAME)
    if (KMOD_COPY_SOURCES)
        file(COPY "${SRC}" DESTINATION "${DEST_DIR}" FOLLOW_SYMLINK_CHAIN)
    else()
        if (EXISTS "${DEST_DIR}/${_base}")
            file(REMOVE "${DEST_DIR}/${_base}")
        endif()
        file(CREATE_LINK "${SRC}" "${DEST_DIR}/${_base}" SYMBOLIC)
    endif()
endfunction()

# -----------------------------------------------------------------------------
# Public API: kmod_target(NAME
#   SOURCES s1.c s2.c ...
#   [HEADERS h1.h ...]
#   [EXTRA_CFLAGS "..."]  # added to kbuild's EXTRA_CFLAGS
#   [MODULE_PARAMS "..."] # passed to insmod target
# )
# -----------------------------------------------------------------------------
function(kmod_target NAME)
    set(oneValueArgs EXTRA_CFLAGS MODULE_PARAMS)
    set(multiValueArgs SOURCES HEADERS)
    cmake_parse_arguments(KMOD "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if (NOT KMOD_SOURCES)
        message(FATAL_ERROR "kmod_target(${NAME}) requires SOURCES")
    endif()

    # Per-target stage dir (inside KMOD_STAGE_ROOT)
    set(STAGE "${KMOD_STAGE_ROOT}/${NAME}")
    file(MAKE_DIRECTORY "${STAGE}")

    # Collect objects & include dirs; stage sources/headers for kbuild
    set(OBJS "")
    set(INCDIRS "")
    foreach(src ${KMOD_SOURCES})
        get_filename_component(SRC_ABS "${src}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        _kmod_stage("${SRC_ABS}" "${STAGE}")
        get_filename_component(BASE "${SRC_ABS}" NAME)
        string(REGEX REPLACE "\\.(c|S)$" ".o" OBJ "${BASE}")
        list(APPEND OBJS "${OBJ}")
        get_filename_component(SDIR "${SRC_ABS}" DIRECTORY)
        list(APPEND INCDIRS "${SDIR}")
    endforeach()

    foreach(h ${KMOD_HEADERS})
        get_filename_component(H_ABS "${h}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        if (EXISTS "${H_ABS}")
            _kmod_stage("${H_ABS}" "${STAGE}")
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

    # Compose EXTRA_CFLAGS (per-target + global) with include dirs for out-of-tree
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
            DEPENDS ${KMOD_SOURCES} ${KMOD_HEADERS} "${STAGE}/Makefile"
            WORKING_DIRECTORY "${STAGE}"
            VERBATIM
    )

    # Real build target that produces the .ko
    add_custom_target(${NAME} DEPENDS "${STAGE}/${NAME}.ko")

    # -----------------------------------------------------------------------------
    # IDE/Indexing helper: OBJECT target with kernel include dirs & defines
    # (EXCLUDE_FROM_ALL => never actually builds; only feeds compile_commands.json)
    # -----------------------------------------------------------------------------
    set(_ide_srcs "")
    foreach(_s ${KMOD_SOURCES} ${KMOD_HEADERS})
        get_filename_component(_abs "${_s}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        list(APPEND _ide_srcs "${_abs}")
    endforeach()

    add_library(${NAME}__ideobj OBJECT ${_ide_srcs})
    set_target_properties(${NAME}__ideobj PROPERTIES EXCLUDE_FROM_ALL YES)

    # Kernel + module includes for IDE parsing (no host headers)
    target_include_directories(${NAME}__ideobj SYSTEM PRIVATE
            ${INCDIRS}
            ${_KINC}
    )

    target_compile_definitions(${NAME}__ideobj PRIVATE
            __KERNEL__
            MODULE
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

    # -----------------------------------------------------------------------------
    # Convenience targets: insmod / rmmod / dmesg / clean
    # -----------------------------------------------------------------------------
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

# -----------------------------------------------------------------------------
# Add your module subdirectories here
# Each subdir should call: kmod_target(<name> SOURCES foo.c [HEADERS ...] [...])
# -----------------------------------------------------------------------------
add_subdirectory(adc)
