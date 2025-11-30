cmake_minimum_required(VERSION 3.21)
project(mmap_dma NONE)
kmod_target(mmap
        SOURCES main.c
        # DEPENDS_MODS mint_class
)
userprog_target(f_mmap
        SOURCES f_mmap.c
        # INCLUDES uac_ioctl.h
        # LIBS m pthread  # add if you need to link extras
        # LIBS m
        REMOTE_DIR /usr/local/bin
        # REMOTE_NAME uacctl
)


