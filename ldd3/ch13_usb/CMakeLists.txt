cmake_minimum_required(VERSION 3.21)
project(usb_driver NONE)
kmod_target(uac SOURCES usb_uac.c HEADERS uac_ioctl.h)
userprog_target(uacctl
        SOURCES uacctl.c
        INCLUDES uac_ioctl.h
        # LIBS m pthread  # add if you need to link extras
        REMOTE_DIR /usr/local/bin
        REMOTE_NAME uacctl
)
