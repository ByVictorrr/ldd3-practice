# ---- Remote Raspberry Pi build helpers (run from your dev machine) ----
option(ENABLE_RPI_REMOTE "Enable remote Pi targets" ON)

# Set these from the CLI or cache
set(RPI_HOST "pi@raspberrypi.local" CACHE STRING "SSH host for the Pi (user@host)")
set(RPI_DIR  "~/dev/ldd3-practice"   CACHE STRING "Remote directory on the Pi")
set(RPI_BUILD_TYPE "RelWithDebInfo"  CACHE STRING "Remote CMake build type")
set(RSYNC_EXCLUDES
        "--exclude=.git --exclude=cmake-build-* --exclude=build --exclude=.idea --exclude=.vscode"
)

if(ENABLE_RPI_REMOTE)
    find_program(RSYNC_EXECUTABLE rsync)
    find_program(SSH_EXECUTABLE ssh)
    if(NOT RSYNC_EXECUTABLE OR NOT SSH_EXECUTABLE)
        message(WARNING "rsync/ssh not found; remote Pi targets will be unavailable.")
    else()
        # 1) Sync your whole repo to the Pi (mirrors local -> remote)
        add_custom_target(rpi-sync
                COMMAND ${RSYNC_EXECUTABLE} -az --delete ${RSYNC_EXCLUDES}
                ${CMAKE_SOURCE_DIR}/ ${RPI_HOST}:${RPI_DIR}/
                USES_TERMINAL
                COMMENT "Sync repo to ${RPI_HOST}:${RPI_DIR}")

        # 2) Configure on the Pi (only the rpi-projects subtree)
        add_custom_target(rpi-configure
                COMMAND ${SSH_EXECUTABLE} ${RPI_HOST}
                "cmake -S ${RPI_DIR}/rpi-projects -B ${RPI_DIR}/rpi-build -DCMAKE_BUILD_TYPE=${RPI_BUILD_TYPE}"
                DEPENDS rpi-sync
                USES_TERMINAL
                COMMENT "CMake configure on Pi in ${RPI_DIR}/rpi-build")

        # 3) Build on the Pi
        add_custom_target(rpi-build
                COMMAND ${SSH_EXECUTABLE} ${RPI_HOST}
                "cmake --build ${RPI_DIR}/rpi-build -j\$(nproc)"
                DEPENDS rpi-configure
                USES_TERMINAL
                COMMENT "Build kernel modules on Pi")

        # 4) Convenience wrappers to run helper targets on the Pi build
        #    (change 'mcp3008_spi' to your module name, or add more targets below)
        add_custom_target(rpi-insmod
                COMMAND ${SSH_EXECUTABLE} ${RPI_HOST}
                "cmake --build ${RPI_DIR}/rpi-build --target mcp3008_spi-insmod"
                DEPENDS rpi-build
                USES_TERMINAL
                COMMENT "insmod mcp3008_spi on Pi")

        add_custom_target(rpi-rmmod
                COMMAND ${SSH_EXECUTABLE} ${RPI_HOST}
                "cmake --build ${RPI_DIR}/rpi-build --target mcp3008_spi-rmmod"
                USES_TERMINAL
                COMMENT "rmmod mcp3008_spi on Pi")

        add_custom_target(rpi-dmesg
                COMMAND ${SSH_EXECUTABLE} ${RPI_HOST}
                "cmake --build ${RPI_DIR}/rpi-build --target mcp3008_spi-dmesg"
                USES_TERMINAL
                COMMENT "tail dmesg on Pi")

        # 5) Meta target
        add_custom_target(rpi ALL DEPENDS rpi-build)
    endif()
endif()
