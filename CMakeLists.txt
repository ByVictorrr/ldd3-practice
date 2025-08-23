cmake_minimum_required(VERSION 3.20)
project(ldd3_practice C)

# Path to *remote* kernel headers
set(KDIR "/lib/modules/${CMAKE_HOST_SYSTEM_VERSION}/build" CACHE PATH "Kernel build dir")

add_subdirectory(ldd3/ch03_scull)   # add more chapters similarly
