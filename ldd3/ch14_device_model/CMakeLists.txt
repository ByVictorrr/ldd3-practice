cmake_minimum_required(VERSION 3.21)
project(device_model NONE)
kmod_target(mintbus_bus
        SOURCES mintbus_bus.c
        HEADERS mintbus.h
)

kmod_target(mintbus_device
        SOURCES mintbus_device.c
        HEADERS mintbus.h
)

kmod_target(mintbus_class
        SOURCES mintbus_class.c
        HEADERS mintbus.h
)

kmod_target(mintbus_driver.c
        SOURCES mintbus_driver.c
        HEADERS mintbus.h
)
