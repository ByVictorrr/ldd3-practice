# CMakeLists.txt — build the user-space tester (not the kernel module)
cmake_minimum_required(VERSION 3.16)
project(scull_user C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(test_scull test_scull.c)
# Assume scull_ioctl.h is in the same source dir; otherwise add include dirs:
target_include_directories(test_scull PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
# No special libs needed on Linux; if you want static, you could add -static here:
target_compile_options(test_scull PRIVATE -Wall -Wextra -O2)
