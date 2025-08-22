cmake_minimum_required(VERSION 3.15)
project(ldd3_ch02_hello C)

# Build the kernel module via Kbuild (your Makefile)
add_custom_target(kbuild ALL
        COMMAND make
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        USES_TERMINAL
        )

# Clean target (can't be named "clean")
add_custom_target(kclean
        COMMAND make clean
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        USES_TERMINAL
        )
