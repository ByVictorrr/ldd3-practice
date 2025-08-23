cmake_minimum_required(VERSION 3.21)
project(scull C)
kmod_target(scull
        SOURCES scull.c
        # EXTRA_CFLAGS "-Wall -Wextra -DDEBUG"
)
# IDE indexing only:
add_custom_target(scull_dummy1 SOURCES scull.c)

