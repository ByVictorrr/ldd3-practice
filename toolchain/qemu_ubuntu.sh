#!/usr/bin/env bash
set -euo pipefail

KSRCDIR=/home/victord/kernels
BZ=$KSRCDIR/linux-stable/arch/x86/boot/bzImage
PROJ=/home/victord/git/ldd3-practice

exec qemu-system-x86_64 -enable-kvm -cpu host -smp 2 -m 2G \
  -kernel "$BZ" \
  -nographic -monitor none \
  -drive file=qemu-image/ldd3.qcow2,if=virtio,format=qcow2 \
  -drive file=qemu-image/seed/seed.iso,if=virtio,media=cdrom,format=raw \
  -serial stdio \
  -chardev socket,id=kgdb,host=127.0.0.1,port=5551,server=on,wait=off,ipv4=on \
  -serial chardev:kgdb \
  -fsdev local,id=fs0,path="$PROJ",security_model=none \
  -device virtio-9p-pci,fsdev=fs0,mount_tag=hostshare \
  -append "root=/dev/vda1 rw rootwait console=ttyS0,115200 earlycon=ttyS0,115200 kgdb.use_kdb=0 kgdboc=ttyS1,115200 nokaslr pcie_ports=native" \
  -netdev user,id=net0,hostfwd=tcp::2222-:22 \
  -device virtio-net-pci,netdev=net0 \
  -machine q35 \
  -device pcie-root-port,id=rp0,chassis=1 \
  -device e1000e,bus=rp0,id=nic0 \
  -device edu,id=edu0 \
  -device pc-testdev
