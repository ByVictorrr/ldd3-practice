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
function(kmod_target NAME)
    set(oneValueArgs EXTRA_CFLAGS MODULE_PARAMS)
    set(multiValueArgs SOURCES HEADERS)
    cmake_parse_arguments(KMOD "" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
    if (NOT KMOD_SOURCES)
        message(FATAL_ERROR "kmod_target(${NAME}) requires SOURCES")
    endif()

    set(STAGE "${CMAKE_BINARY_DIR}/kmods/${NAME}")
    file(MAKE_DIRECTORY "${STAGE}")

    set(OBJS "")
    set(INCDIRS "")
    foreach(src ${KMOD_SOURCES})
        get_filename_component(SRC_ABS "${src}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        get_filename_component(BASE "${SRC_ABS}" NAME)
        file(CREATE_LINK "${SRC_ABS}" "${STAGE}/${BASE}" SYMBOLIC)
        string(REGEX REPLACE "\\.(c|S)$" ".o" OBJ "${BASE}")
        list(APPEND OBJS "${OBJ}")
        get_filename_component(SDIR "${SRC_ABS}" DIRECTORY)
        list(APPEND INCDIRS "${SDIR}")
    endforeach()

    foreach(h ${KMOD_HEADERS})
        get_filename_component(H_ABS "${h}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        if (EXISTS "${H_ABS}")
            get_filename_component(HBASE "${H_ABS}" NAME)
            file(CREATE_LINK "${H_ABS}" "${STAGE}/${HBASE}" SYMBOLIC)
            get_filename_component(HDIR "${H_ABS}" DIRECTORY)
            list(APPEND INCDIRS "${HDIR}")
        endif()
    endforeach()
    list(REMOVE_DUPLICATES INCDIRS)

    set(MK "obj-m += ${NAME}.o\n")
    set(REST_OBJS "${OBJS}")
    list(REMOVE_ITEM REST_OBJS "${NAME}.o")
    if (REST_OBJS)
        string(REPLACE ";" " " REST_OBJS "${REST_OBJS}")
        string(APPEND MK "${NAME}-objs := ${REST_OBJS}\n")
    endif()

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
    # Absolute paths for reliable mapping
    set(_ide_srcs "")
    foreach(_s ${KMOD_SOURCES} ${KMOD_HEADERS})
        get_filename_component(_abs "${_s}" ABSOLUTE BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
        list(APPEND _ide_srcs "${_abs}")
    endforeach()

    # Option A (most reliable across CLion versions)
    add_custom_target(${NAME}-ide SOURCES ${_ide_srcs})

    # Option B (works in newer CLion/CMake too; keep both is fine)
    add_library(${NAME}_ide INTERFACE)
    target_sources(${NAME}_ide INTERFACE ${_ide_srcs})

    # Attach sources for IDE (no build effect)
    ###################3

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
