cmake_minimum_required(VERSION 3.16)
project(rpi_kmods NONE)

# Avoid try-compile link checks; kbuild does the real work
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Detect running kernel/arch (when configuring on the Pi)
execute_process(COMMAND uname -r OUTPUT_VARIABLE KREL OUTPUT_STRIP_TRAILING_WHITESPACE)
execute_process(COMMAND uname -m OUTPUT_VARIABLE KMACH OUTPUT_STRIP_TRAILING_WHITESPACE)

if (KMACH MATCHES "^(aarch64|arm64)$")
    set(KARCH arm64)
elseif (KMACH MATCHES "^arm")
    set(KARCH arm)
else()
    set(KARCH x86)
endif()

# Kernel headers (override with -DKDIR=...)
set(KDIR "/lib/modules/${KREL}/build" CACHE PATH "Kernel build dir")
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

# ---------------------- NEW: staging overrides & helper ----------------------
# Where to stage (symlink/copy) sources for kbuild. You can override this:
#   -DKMOD_STAGE_ROOT=/dev/shm/kmods
set(KMOD_STAGE_ROOT "${CMAKE_BINARY_DIR}/kmods"
        CACHE PATH "Root directory where kmod sources are staged for kbuild")

# Some environments/IDEs dislike symlinks. Flip this to copy instead.
#   -DKMOD_COPY_SOURCES=ON
option(KMOD_COPY_SOURCES "Copy sources instead of creating symlinks" OFF)

# Helper to stage files (copy or symlink) into the per-target STAGE dir
function(_kmod_stage SRC DEST_DIR)
    file(MAKE_DIRECTORY "${DEST_DIR}")
    get_filename_component(_base "${SRC}" NAME)
    if (KMOD_COPY_SOURCES)
        file(COPY "${SRC}" DESTINATION "${DEST_DIR}" FOLLOW_SYMLINK_CHAIN)
    else()
        # Overwrite if exists to keep re-config idempotent
        if (EXISTS "${DEST_DIR}/${_base}")
            file(REMOVE "${DEST_DIR}/${_base}")
        endif()
        file(CREATE_LINK "${SRC}" "${DEST_DIR}/${_base}" SYMBOLIC)
    endif()
endfunction()
# ---------------------------------------------------------------------------

function(kmod_target NAME)
    set(oneValueArgs EXTRA_CFLAGS MODULE_PARAMS)
    set(multiValueArgs SOURCES HEADERS)
    cmake_parse_arguments(KMOD "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
    if (NOT KMOD_SOURCES)
        message(FATAL_ERROR "kmod_target(${NAME}) requires SOURCES")
    endif()

    # ---------- CHANGED: use configurable stage root instead of CMAKE_BINARY_DIR ----------
    set(STAGE "${KMOD_STAGE_ROOT}/${NAME}")
    file(MAKE_DIRECTORY "${STAGE}")

    set(OBJS "")
    set(INCDIRS "")

    # Stage sources and build object list
    foreach(src ${KMOD_SOURCES})
        get_filename_component(SRC_ABS "${src}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        _kmod_stage("${SRC_ABS}" "${STAGE}")
        get_filename_component(BASE "${SRC_ABS}" NAME)
        string(REGEX REPLACE "\\.(c|S)$" ".o" OBJ "${BASE}")
        list(APPEND OBJS "${OBJ}")
        get_filename_component(SDIR "${SRC_ABS}" DIRECTORY)
        list(APPEND INCDIRS "${SDIR}")
    endforeach()

    # Stage headers (optional)
    foreach(h ${KMOD_HEADERS})
        get_filename_component(H_ABS "${h}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        if (EXISTS "${H_ABS}")
            _kmod_stage("${H_ABS}" "${STAGE}")
            get_filename_component(HDIR "${H_ABS}" DIRECTORY)
            list(APPEND INCDIRS "${HDIR}")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES INCDIRS)

    # Generate kbuild Makefile for this module
    set(MK "obj-m += ${NAME}.o\n")
    set(REST_OBJS "${OBJS}")
    list(REMOVE_ITEM REST_OBJS "${NAME}.o")
    if (REST_OBJS)
        string(REPLACE ";" " " REST_OBJS "${REST_OBJS}")
        string(APPEND MK "${NAME}-objs := ${REST_OBJS}\n")
    endif()

    # Compose include flags for out-of-tree build
    set(INCFLAGS "")
    foreach(d ${INCDIRS})
        string(APPEND INCFLAGS " -I${d}")
    endforeach()
    if (KMOD_EXTRA_CFLAGS)
        string(APPEND MK "EXTRA_CFLAGS += ${KMOD_EXTRA_CFLAGS}${INCFLAGS}\n")
    else()
        string(APPEND MK "EXTRA_CFLAGS +=${INCFLAGS}\n")
    endif()

    file(WRITE "${STAGE}/Makefile" "${MK}")

    find_program(MAKE_CMD NAMES make gmake REQUIRED)
    add_custom_command(
            OUTPUT "${STAGE}/${NAME}.ko"
            COMMAND "${MAKE_CMD}" -C "${KDIR}" M=${STAGE} ARCH=${KARCH} modules
            DEPENDS ${KMOD_SOURCES} ${KMOD_HEADERS} "${STAGE}/Makefile"
            WORKING_DIRECTORY "${STAGE}"
            VERBATIM
    )
    add_custom_target(${NAME} DEPENDS "${STAGE}/${NAME}.ko")

    # --- IDE indexing: attach sources so CLion knows they belong to a target ---
    set(_ide_srcs "")
    foreach(_s ${KMOD_SOURCES} ${KMOD_HEADERS})
        get_filename_component(_abs "${_s}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        list(APPEND _ide_srcs "${_abs}")
    endforeach()

    add_custom_target(${NAME}-ide SOURCES ${_ide_srcs})

    add_library(${NAME}_ide INTERFACE)
    target_sources(${NAME}_ide INTERFACE ${_ide_srcs})
    # ---------------------------------------------------------------------------

    # Convenience targets: insmod/rmmod/dmesg
    set(_params "")
    if (KMOD_MODULE_PARAMS)
        set(_params "${KMOD_MODULE_PARAMS}")
    endif()

    add_custom_target(${NAME}-insmod
            DEPENDS ${NAME}
            COMMAND sudo insmod "${STAGE}/${NAME}.ko" ${_params} || (dmesg | tail -n 200; false)
            USES_TERMINAL)

    add_custom_target(${NAME}-rmmod
            COMMAND sudo rmmod -f ${NAME} || true
            USES_TERMINAL)

    add_custom_target(${NAME}-dmesg
            COMMAND dmesg | tail -n 200
            USES_TERMINAL)
endfunction()

# Add your module subdirs here (create at least one)
add_subdirectory(adc)
