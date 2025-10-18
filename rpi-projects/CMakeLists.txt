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

# Autodetect KSRC if not explicitly provided
if (NOT KSRC)
    if (EXISTS "${KDIR}/source/include/linux")
        set(KSRC "${KDIR}/source")
    else()
        # fall back to KDIR; works for monolithic trees
        set(KSRC "${KDIR}")
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

# Warn if UAPI headers missing from KSRC (CLion index needs these)
if (NOT EXISTS "${KSRC}/include/uapi/linux/types.h")
    message(WARNING
        "KSRC appears to be missing include/uapi/linux/types.h.\n"
        "  KSRC=${KSRC}\n"
        "CLion's parser will fail to resolve u8/u16/... without UAPI.\n"
        "Fix by installing/pointing KSRC to the kernel source package, e.g.:\n"
        "  cmake -S . -B build -DKDIR=${KDIR} -DKSRC=/usr/src/<rpi-linux-*>"
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
# Root where each module's sources are staged for kbuild
# Override for tmpfs speed: -DKMOD_STAGE_ROOT=/dev/shm/kmods
set(KMOD_STAGE_ROOT "${CMAKE_BINARY_DIR}/kmods"
    CACHE PATH "Root directory where kmod sources are staged for kbuild")

# Some IDEs / remote setups dislike symlinks; copy when ON (default ON for SSH)
option(KMOD_COPY_SOURCES "Copy sources instead of creating symlinks" ON)

# Extra flags passed to kbuild via EXTRA_CFLAGS (all modules)
set(KMOD_GLOBAL_EXTRA_CFLAGS "" CACHE STRING "Global extra C flags for all kmods")

# Verbose kbuild (make V=1)
option(KMOD_VERBOSE "Enable verbose kbuild output" OFF)

# -----------------------------------------------------------------------------
# Kernel include directories used by IDE helper targets (for parsing only)
# NOTE:
#   - Generated headers come from ${KDIR}/include/generated
#   - Source headers (linux/*, uapi/*, arch/*) come from ${KSRC}
#   These are NOT injected into kbuild; they are for IDE code model only.
# -----------------------------------------------------------------------------
# Existing kernel include roots
set(_KINC
        ${KSRC}/include
        ${KSRC}/include/uapi
        ${KDIR}/include/generated
        ${KDIR}/include/generated/uapi
)

# Arch-specific *source* includes (if present)
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

# >>> NEW: Arch-specific *BUILD* (generated) includes from KDIR <<<
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

# Keep generated first in search order
list(REMOVE_ITEM _KINC ${KDIR}/include/generated ${KDIR}/include/generated/uapi)
list(INSERT _KINC 0
        ${KDIR}/include/generated
        ${KDIR}/include/generated/uapi
)


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
    # We only add module source include dirs (INCDIRS), not kernel dirs.
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

    # Let kbuild decide arch when building on-target; otherwise pass ARCH/CROSS_COMPILE
    add_custom_command(
        OUTPUT "${STAGE}/${NAME}.ko"
        COMMAND "${MAKE_CMD}" -C "${KDIR}" M=${STAGE} ${_vflag} modules
        # If cross-compiling:
        # COMMAND "${MAKE_CMD}" -C "${KDIR}" M=${STAGE} ARCH=${KARCH} CROSS_COMPILE=aarch64-linux-gnu- ${_vflag} modules
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

    # IMPORTANT: only module/local includes + kernel includes for the IDE
    target_include_directories(${NAME}__ideobj SYSTEM PRIVATE
        ${INCDIRS}
        ${_KINC}
    )

    # Tell the parser it's kernel C, not userspace/C++
    target_compile_definitions(${NAME}__ideobj PRIVATE
        __KERNEL__
        MODULE
        # $<$<STREQUAL:${KARCH},arm>:__ARM_ARCH=7>
        $<$<STREQUAL:${KARCH},arm64>:__aarch64__>
    )

    # CRUCIAL: keep host headers out so u8 resolves from <linux/types.h>
    target_compile_options(${NAME}__ideobj PRIVATE
        -std=gnu11
        -nostdinc
        -Wno-declaration-after-statement
        -Wno-unused-parameter
        -Wno-sign-compare
        -Wno-missing-field-initializers
    )

    # Optional: simple display target in project tree (no build)
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
