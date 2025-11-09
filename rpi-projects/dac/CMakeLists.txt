project(dac)
# Register the modules you have in rpi-projects/ (add more as needed)
cmake_minimum_required(VERSION 3.21)
# Attach source files for THIS module here:
# rpi-projects/adc/CMakeLists.txt

kmod_target(pcm5102a_i2s
        SOURCES pcm5102a_i2s.c
        HEADERS pcm5102a_regs.h
        # EXTRA_CFLAGS "-Wall -Wextra -O2"
        # MODULE_PARAMS "debug=1 rate_hz=500000"
)

