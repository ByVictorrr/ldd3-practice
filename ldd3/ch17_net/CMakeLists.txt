cmake_minimum_required(VERSION 3.21)
project(net_impair NONE)

kmod_target(impair
        SOURCES impair_main.c impair_ring.c
        # HEADERS edu_ioctl.h
        # DEPENDS_MODS mint_class
)
