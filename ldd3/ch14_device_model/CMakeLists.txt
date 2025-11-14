cmake_minimum_required(VERSION 3.21)
project(device_model NONE)
kmod_target(mint_bus
        SOURCES mint_bus.c mint_utils.c
        HEADERS mint.h
)


kmod_target(mint_driver
        SOURCES mint_driver.c mint_utils.c
        HEADERS mint.h
)
kmod_target(mint_class
        SOURCES mint_class.c
        HEADERS mint.h
)

