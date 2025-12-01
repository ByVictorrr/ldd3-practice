cmake_minimum_required(VERSION 3.21)
project(block NONE)

kmod_target(block
        SOURCES block.c
        # HEADERS edu_ioctl.h
        # DEPENDS_MODS mint_class
)
set(REMOTE_BIN_DIR "/usr/local/bin")
