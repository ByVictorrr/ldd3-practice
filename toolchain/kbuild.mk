# Set KDIR if not provided (running kernel headers)
KDIR ?= /lib/modules/$(shell uname -r)/build
