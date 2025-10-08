# project(adc)
# Register the modules you have in rpi-projects/ (add more as needed)
cmake_minimum_required(VERSION 3.21)
# Attach source files for THIS module here:
# rpi-projects/adc/CMakeLists.txt
kmod_target(mcp3008_spi
        SOURCES mcp3008_spi.c
        # HEADERS mcp3008.h
        # EXTRA_CFLAGS "-Wall -Wextra -O2"
        # MODULE_PARAMS "debug=1 rate_hz=500000"
)

