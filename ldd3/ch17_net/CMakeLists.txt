cmake_minimum_required(VERSION 3.21)
project(net_impair NONE)

kmod_target(impair
        SOURCES impair_main.c impair_ring.c impair_qvec.c
        HEADERS impair.h
        # HEADERS edu_ioctl.h
        # DEPENDS_MODS mint_class
)
set(REMOTE_BIN_DIR "/usr/local/bin")
set(IFACE "impair0")
set(COUNT "50")

remote_scp_and_run(impair_tests_all
        LOCAL_PATH  ${CMAKE_CURRENT_SOURCE_DIR}/impair_tests_pktgen.sh
        REMOTE_PATH ${REMOTE_BIN_DIR}/impair_tests_pktgen.sh
        SSH_CMDS
        "chmod +x ${REMOTE_BIN_DIR}/impair_tests_pktgen.sh"
        "ip link set dev ${IFACE} up"
        "IFACE=${IFACE} COUNT=${COUNT} ${REMOTE_BIN_DIR}/impair_tests_pktgen.sh all"
)
