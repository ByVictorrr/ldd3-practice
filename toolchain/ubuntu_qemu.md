# QEMU Guest with Your Kernel + Apt/SSH (Quick Guide)

A minimal, repeatable setup to boot a **real distro rootfs** (Ubuntu 24.04 minimal cloud image) under QEMU **using your own kernel**. You’ll get `apt`, `sshd`, persistence, and an easy way to copy/debug binaries — while keeping your host untouched.

---

## 0) Prerequisites (host)

* QEMU (x86_64): `qemu-system-x86_64`, `qemu-img`
* `xorriso` **or** `genisoimage` (for cloud-init seed ISO)
* Your built kernel bzImage (x86_64) with these **built-in** (no initrd required):

    * `CONFIG_PCI=y`, `CONFIG_VIRTIO_PCI=y`, `CONFIG_VIRTIO_BLK=y`
    * `CONFIG_EXT4_FS=y`, `CONFIG_DEVTMPFS=y`, `CONFIG_DEVTMPFS_MOUNT=y`
    * `CONFIG_EFI_PARTITION=y`
    * Make sure **`CONFIG_CMDLINE_FORCE` is not set**

---

## 1) Prepare a persistent disk image

```bash
mkdir -p ~/qemu-vms/edu-guest && cd ~/qemu-vms/edu-guest
# Download Ubuntu 24.04 amd64 minimal cloud image
wget https://cloud-images.ubuntu.com/minimal/releases/noble/release/ubuntu-24.04-minimal-cloudimg-amd64.img -O ubuntu-amd64.qcow2
# Create a writable overlay (keeps the base read-only)
qemu-img create -f qcow2 -F qcow2 -b ubuntu-amd64.qcow2 ldd3.qcow2 30G
```

> You’ll boot from **ldd3.qcow2** so all package installs persist there.

---

## 2) Create a NoCloud seed ISO (login + networking)

```bash
mkdir -p ~/qemu-seed && cd ~/qemu-seed
cat > user-data <<'YAML'
#cloud-config
users:
  - name: root
    lock_passwd: false
    ssh_authorized_keys:
      - ssh-ed25519 AAAA...REPLACE_WITH_YOUR_PUBKEY
chpasswd:
  list: |
    root:root
  expire: false
ssh_pwauth: true
YAML
cat > meta-data <<'EOF'
instance-id: iid-edu-001
local-hostname: edu-guest
EOF
# Build the ISO (either command works; use whichever tool you have)
# genisoimage -output seed.iso -volid CIDATA -joliet -rock user-data meta-data
xorriso -as mkisofs -output seed.iso -volid CIDATA -joliet -rock user-data meta-data
```

---

## 3) Run script (boots your kernel against the qcow2 root)

Create `run-qemu.sh` (edit paths to match your env):

```bash
#!/usr/bin/env bash
set -euo pipefail

KSRCDIR=/home/victord/kernels
BZ=$KSRCDIR/linux-stable/arch/x86/boot/bzImage
PROJ=/home/victord/git/ldd3-practice
IMG=/home/victord/git/ldd3-practice/qemu-image/ldd3.qcow2   # or ~/qemu-vms/edu-guest/ldd3.qcow2
SEED=/home/victord/git/ldd3-practice/qemu-image/seed/seed.iso # or ~/qemu-seed/seed.iso

exec qemu-system-x86_64 -enable-kvm -cpu host -smp 2 -m 2G \
  -kernel "$BZ" \
  -machine q35 \
  -nographic -monitor none \
  -serial stdio \
  -drive file="$IMG",if=virtio,format=qcow2 \
  -drive file="$SEED",if=virtio,media=cdrom,format=raw \
  -device virtio-net-pci,netdev=net0 \
  -netdev user,id=net0,hostfwd=tcp::2222-:22 \
  -fsdev local,id=fs0,path="$PROJ",security_model=none \
  -device virtio-9p-pci,fsdev=fs0,mount_tag=hostshare \
  -device pcie-root-port,id=rp0,chassis=1 \
  -device edu,bus=rp0,id=edu0 \
  -append "root=/dev/vda1 rw rootwait console=ttyS0,115200 earlycon=ttyS0,115200 nokaslr kgdb.use_kdb=0 kgdboc=ttyS1,115200 pcie_ports=native"
```

Make it executable: `chmod +x run-qemu.sh` and run it.

> **Why `/dev/vda1`?** The cloud image has the root filesystem on the first partition. Some kernels don’t resolve `root=LABEL=cloudimg-rootfs` early enough; `/dev/vda1` is robust.

---

## 4) First boot inside the guest

Log in from host:

```bash
ssh -p 2222 root@localhost   # password 'root' unless you changed it
```

Install basics:

```bash
apt update
apt install -y openssh-server build-essential pciutils usbutils git vim
systemctl enable --now ssh
# Mount your host project folder (9p share)
mkdir -p /mnt/host && mount -t 9p -o trans=virtio hostshare /mnt/host
```

Now you can copy/run your tools, or even build inside the guest with apt packages available.

---

## 5) (Optional) CMake SCP target to copy a binary into the guest

```cmake
set(SCP_BIN scp)
set(SSH_BIN ssh)
set(SCP_ARGS "-P 2222 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null")
add_custom_target(aer_inject-scp
    DEPENDS aer_inject
    COMMAND ${SCP_BIN} ${SCP_ARGS} $<TARGET_FILE:aer_inject> root@localhost:/tmp/aer_inject
    COMMAND ${SSH_BIN} -p 2222 -o StrictHostKeyChecking=no -o UserKnownHostsFile=/dev/null root@localhost chmod +x /tmp/aer_inject
    USES_TERMINAL
)
```

---

## 6) Troubleshooting

* **Panic: Unable to mount root fs**

    * Ensure `root=/dev/vda1 rootwait` in `-append`.
    * Confirm kernel has `VIRTIO_PCI`, `VIRTIO_BLK`, `EXT4`, `DEVTMPFS`, `EFI_PARTITION` built-in (`=y`).
    * Verify `-drive file=...,if=virtio` (not scsi/nvme unless you intend `/dev/sdX` or `/dev/nvme0n1p1`).
* **No login / no SSH**

    * Confirm `seed.iso` is attached and cloud-init ran. Check `journalctl -u ssh` inside guest. You can always log into the serial console (the QEMU window/stdout).
* **9p share won’t mount**

    * Ensure both `-fsdev ...` and `-device virtio-9p-pci,...` are on the QEMU line, and mount with `mount -t 9p -o trans=virtio hostshare /mnt/host`.
* **AER testing**

    * You already have a Root Port + `edu` endpoint. Use your `aer_inject` binary or QEMU monitor tools to inject errors and observe your driver’s AER callbacks.

---

## 7) Optional: use labels/UUID

If you prefer labels/UUID and your kernel supports early resolution, replace `root=/dev/vda1` with one of:

```bash
-append "root=LABEL=cloudimg-rootfs ro rootwait console=ttyS0,115200 ..."
-append "root=UUID=<your-uuid> ro rootwait console=ttyS0,115200 ..."
```

Find them on the host:

```bash
sudo modprobe nbd max_part=8
sudo qemu-nbd --connect=/dev/nbd0 ~/qemu-vms/edu-guest/ldd3.qcow2
lsblk -f /dev/nbd0
sudo qemu-nbd --disconnect /dev/nbd0
```

That “Unable to locate package vim” means your guest doesn’t yet have its APT sources initialized — it’s a **cloud image** expecting to pull them from official Ubuntu mirrors after cloud-init runs.

Here’s how to fix it inside the VM:

---

### 1️⃣ Check your `/etc/apt/sources.list`

If it’s empty or just has a `cdrom:` line, replace it with a normal mirror set:

```bash
cat <<'EOF' > /etc/apt/sources.list
deb http://archive.ubuntu.com/ubuntu/ noble main restricted universe multiverse
deb http://archive.ubuntu.com/ubuntu/ noble-updates main restricted universe multiverse
deb http://archive.ubuntu.com/ubuntu/ noble-security main restricted universe multiverse
deb http://archive.ubuntu.com/ubuntu/ noble-backports main restricted universe multiverse
EOF
```

*(If you’re using Ubuntu 24.04 LTS, its codename is **noble**. Use “jammy” for 22.04, “focal” for 20.04, etc.)*

---

### 2️⃣ Refresh APT & install Vim

```bash
apt update
apt install vim -y
```

If you get key or HTTPS errors:

```bash
apt install -y ca-certificates apt-transport-https
apt update
```

---

### 3️⃣ Optional: verify network

Cloud images rely on DHCP and NAT from QEMU; confirm you have outbound connectivity:

```bash
ping -c2 archive.ubuntu.com
```

If not, make sure your QEMU command line includes user-mode networking:

```
-netdev user,id=net0,hostfwd=tcp::2222-:22
-device virtio-net-pci,netdev=net0
```

---

### 4️⃣ (Optional) minimal alternative

If you just need an editor quickly:

```bash
apt install nano -y
```

---

Once your `/etc/apt/sources.list` is populated and networking works, `apt install vim` (and other tools) will function normally.
