# Reuse a shared include so flags/paths are consistent
-include ../../toolchain/kbuild.mk

obj-m += hello.o
obj-m += user.o

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
