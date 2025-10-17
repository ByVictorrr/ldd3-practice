cmake_minimum_required(VERSION 3.16)

# -----------------------------------------------------------------------------
# Project & global config
# -----------------------------------------------------------------------------
project(rpi_kmods )
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_C_STANDARD 11)

# -----------------------------------------------------------------------------
# Detect running kernel / arch (when configuring on the Pi)
# -----------------------------------------------------------------------------
execute_process(COMMAND uname -r OUTPUT_VARIABLE KREL OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND uname -m OUTPUT_VARIABLE KMACH OUTPUT_STRIP_TRAILING_WHITESPACE)

if (KMACH MATCHES "^(aarch64|arm64)$")
    set(KARCH arm64)
else()
    set(KARCH x86)  # fallback if configuring off-target
endif()

# Kernel build dir (can be overridden: -DKDIR=/path/to/kernel/build)
set(KDIR "/lib/modules/${KREL}/build" CACHE PATH "Kernel build directory (kbuild)")

# In split layouts, headers like include/linux/init.h live under ${KDIR}/source
if (EXISTS "${KDIR}/source/include/linux")
    set(KSRC "${KDIR}/source")
else()
    set(KSRC "${KDIR}")
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
# Kernel include directories used by IDE helper targets
# NOTE:
#   - Generated headers come from ${KDIR}/include/generated
#   - Source headers (linux/*, uapi/*, arch/*) come from ${KSRC}
# -----------------------------------------------------------------------------
set(_KINC
        ${KSRC}/include
        ${KSRC}/include/uapi
        ${KSRC}/arch/arm/include
        ${KSRC}/arch/arm/include/uapi
        ${KDIR}/include/generated
        ${KDIR}/include/generated/uapi
)

# (Optional) Also inject globally so standalone files in the IDE parse decently
include_directories(${_KINC})

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
            # COMMAND "${MAKE_CMD}" -C "${KDIR}" M=${STAGE} ARCH=${KARCH} ${_vflag} modules
            COMMAND "${MAKE_CMD}" -C "${KDIR}" M=${STAGE} ARCH=arm64 ${_vflag} modules
            DEPENDS ${KMOD_SOURCES} ${KMOD_HEADERS} "${STAGE}/Makefile"
            # DEPENDS ${KMOD_SOURCES} ${KMOD_HEADERS} "${STAGE}/Makefile"
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

    target_include_directories(${NAME}__ideobj PRIVATE
            ${INCDIRS}
            ${_KINC}
    )

    target_compile_definitions(${NAME}__ideobj PRIVATE
            __KERNEL__
            MODULE
            # Hint the parser about arch (purely for code model)
            $<$<STREQUAL:${KARCH},arm>:__ARM_ARCH=7>
            $<$<STREQUAL:${KARCH},arm64>:__aarch64__>
    )

    target_compile_options(${NAME}__ideobj PRIVATE
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
# Each subdir typically calls: kmod_target(<name> SOURCES foo.c [HEADERS ...] [...])
# -----------------------------------------------------------------------------
add_subdirectory(adc)
