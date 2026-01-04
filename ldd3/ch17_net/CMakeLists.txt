cmake_minimum_required(VERSION 3.21)
project(net_impair NONE)

kmod_target(net_impair
        SOURCES net_impair.c
        # HEADERS edu_ioctl.h
        # DEPENDS_MODS mint_class
)
