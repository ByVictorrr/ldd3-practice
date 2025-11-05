#!/usr/bin/env bash
set -euo pipefail

KSRCDIR=/home/victord/kernels
BZ=$KSRCDIR/linux-stable/arch/x86/boot/bzImage
PROJ=/home/victord/git/ldd3-practice
FS=$PROJ/toolchain/qemu-image/ldd3.qcow2
SEED_ISO=$PROJ/toolchain/qemu-image/seed/seed.iso
CMD_LINE="root=/dev/vda1 rw rootwait \
console=ttyS0,115200 earlycon=ttyS0,115200 \
kgdb.use_kdb=0 kgdboc=ttyS1,115200 \
nokaslr pcie_ports=native \
dummy_hcd.is_high_speed=0 dummy_hcd.is_super_speed=0 dummy_hcd.num=1 \
g_serial.enable=0"
exec qemu-system-x86_64 -enable-kvm -cpu host -smp 2 -m 2G \
  -kernel "$BZ" \
  -nographic -monitor none \
  -drive file=$FS,if=virtio,format=qcow2,index=0 \
  -drive file=$SEED_ISO,if=virtio,media=cdrom,format=raw,index=1 \
  -serial stdio \
  -chardev socket,id=kgdb,host=127.0.0.1,port=5551,server=on,wait=off,ipv4=on \
  -serial chardev:kgdb \
  -fsdev local,id=fs0,path="$PROJ",security_model=none \
  -device virtio-9p-pci,fsdev=fs0,mount_tag=hostshare \
  -append "${CMD_LINE}" \
  -netdev user,id=net0,hostfwd=tcp::2222-:22,hostfwd=udp::5557-:5555 \
  -device virtio-net-pci,netdev=net0 \
  -machine q35 \
  -device pcie-root-port,id=rp0,chassis=1 \
  -device e1000e,bus=rp0,id=nic0 \
  -device edu,id=edu0 \
  -device pc-testdev \
  -audiodev none,id=nomap \
  -audiodev sdl,id=snd0 \
  -device qemu-xhci,id=xhci \
  -device usb-audio,audiodev=snd0,bus=xhci.0 \
  -device intel-hda -device hda-duplex,audiodev=snd0 \
  -audiodev sdl,id=snd0
   # -device usb-audio,audiodev=cap0,bus=xhci.0 \

