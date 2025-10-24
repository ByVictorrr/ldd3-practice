add_executable(aer_inject /home/victord/kernels/tools/aer-inject/)
target_link_options(aer_inject PRIVATE -static)
# ---- scp the built binary into the guest and chmod it ----
add_custom_target(aer_inject-scp
DEPENDS aer_inject edu_pci-insmod
COMMAND ${SCP_BIN} ${SCP_ARGS} $<TARGET_FILE:aer_inject> root@localhost:/tmp/aer_inject
COMMAND ${SSH_BIN} ${SSH_ARGS} chmod +x /tmp/aer_inject
USES_TERMINAL
COMMENT "Copying aer_inject to guest and making it executable"
). echo hi > /dev/scull0