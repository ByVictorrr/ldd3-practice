cmake_minimum_required(VERSION 3.21)
project(device_model NONE)
kmod_target(mint_bus
        SOURCES mint_bus.c
        HEADERS mint.h
        # DEPENDS_MODS mint_class
)


kmod_target(mint_driver
        SOURCES mint_driver.c
        HEADERS mint.h
        DEPENDS_MODS mint_bus
)
kmod_target(mint_class
        SOURCES mint_class.c
        HEADERS mint.h
        DEPENDS_MODS mint_bus
)

remote_scp_and_run(push_best_udev_rule
        LOCAL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/99-best-mint.rules"
        REMOTE_PATH "/etc/udev/rules.d/99-best-mint.rules"
        SSH_CMDS
            "udevadm control --reload"
            "udevadm trigger"
)

