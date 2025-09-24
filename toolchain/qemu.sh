#!/usr/bin/env bash
set -euo pipefail

KSRCDIR=/home/victord/linux-6.6.103
BZ=$KSRCDIR/arch/x86/boot/bzImage
INITRD=/home/victord/rootfs/initramfs.cpio.gz
PROJ=/home/victord/CLionProjects/ldd3-practice

exec qemu-system-x86_64 -enable-kvm -cpu host -smp 2 -m 2G \
  -kernel "$BZ" -initrd "$INITRD" \
  -nographic -monitor none \
  -serial stdio \
  -chardev socket,id=kgdb,host=127.0.0.1,port=5551,server=on,wait=off,ipv4=on \
  -serial chardev:kgdb \
  -fsdev local,id=fs0,path="$PROJ",security_model=none \
  -device virtio-9p-pci,fsdev=fs0,mount_tag=hostshare \
  -append "kgdb.use_kdb=0 kgdboc=ttyS1,115200 console=ttyS0,115200 earlycon=ttyS0,115200 nokaslr rdinit=/init"
  # -append "kgdb.use_kdb=0 kgdboc=ttyS1,115200 console=ttyS0,115200 earlycon=ttyS0,115200 nokaslr rdinit=/init kgdbwait" \
