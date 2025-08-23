cmake_minimum_required(VERSION 3.16)
project(ch02 NONE)

# Build the simple modules in this chapter
kmod_target(hello
        SOURCES hello.c
        EXTRA_CFLAGS "-I${CMAKE_CURRENT_SOURCE_DIR}" # include headers in cwd
        # EXTRA_CFLAGS "-Wall -Wextra -DDEBUG"
)

kmod_target(user
        SOURCES user.c
        EXTRA_CFLAGS "-I${CMAKE_CURRENT_SOURCE_DIR}" # include headers in cwd
        # EXTRA_CFLAGS "-Wall -Wextra -DDEBUG"
)
