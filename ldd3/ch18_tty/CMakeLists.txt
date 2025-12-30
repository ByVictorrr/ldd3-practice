cmake_minimum_required(VERSION 3.21)
project(tty NONE)

kmod_target(echo_tty
        SOURCES echo_tty.c
        # HEADERS edu_ioctl.h
        # DEPENDS_MODS mint_class
)
# set(REMOTE_BIN_DIR "/usr/local/bin")
