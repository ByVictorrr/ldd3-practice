cat /sys/bus/pci/devices/0000:00:05.0/power/runtime_status
echo auto > /sys/bus/pci/devices/0000:00:05.0/power/control
echo 2000 > /sys/bus/pci/devices/0000:00:05.0/power/autosuspend_delay_ms
watch -n 0.5 cat /sys/bus/pci/devices/0000:00:05.0/power/runtime_status