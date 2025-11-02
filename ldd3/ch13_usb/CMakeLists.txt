cmake_minimum_required(VERSION 3.21)
project(usb_driver NONE)
kmod_target(uac SOURCES usb_uac.c HEADERS uac_ioctl.h)
