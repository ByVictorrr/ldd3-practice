#!/usr/bin/env bash
set -euo pipefail

KSRCDIR=/home/victord/kernels
BZ=$KSRCDIR/linux-stable/arch/x86/boot/bzImage
INITRD=$KSRCDIR/initrd.cpio.gz
PROJ=/home/victord/git/ldd3-practice

exec qemu-system-x86_64 -enable-kvm -cpu host -smp 2 -m 2G \
  -kernel "$BZ" -initrd "$INITRD" \
  -nographic -monitor none \
  -serial stdio \
  -chardev socket,id=kgdb,host=127.0.0.1,port=5551,server=on,wait=off,ipv4=on \
  -serial chardev:kgdb \
  -fsdev local,id=fs0,path="$PROJ",security_model=none \
  -append "kgdb.use_kdb=0 kgdboc=ttyS1,115200 console=ttyS0,115200 earlycon=ttyS0,115200 nokaslr rdinit=/init pcie_ports=native" \
  -netdev user,id=net0,hostfwd=tcp::2222-:22 \
  -device virtio-net-pci,netdev=net0 \
  -nic user,model=e1000 \
  -device pc-testdev \
  -machine q35 \
  -device pcie-root-port,id=rp0,chassis=1 \
  -device edu,bus=rp0,id=edu0


# edu ls /sys/bus/pci/devices/0000:00:04.0
