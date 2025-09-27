# 🐧 Building & Booting a Custom Linux Kernel with **Dropbear** SSH Access in QEMU (musl-static, with headers)

This is a clean, end-to-end guide that:

* Builds a custom kernel + modules **and installs userspace headers**.
* Creates a BusyBox-based initramfs.
* Builds **musl-static zlib** and **musl-static Dropbear** (pubkey-only).
* Uses the **Dropbear multi-binary** at the correct path with proper symlinks.

---

## 1) Prerequisites (Arch/EndeavourOS)

```bash
sudo pacman -S --needed \
  base-devel bc bison flex libelf elfutils pahole cpio zstd xz ncurses git \
  qemu-full musl autoconf automake libtool pkgconf curl
```

---

## 2) Fetch Kernel

```bash
mkdir -p ~/kernels/{src,build}
cd ~/kernels/src
git clone https://git.kernel.org/pub/scm/linux/kernel/git/stable/linux.git linux-stable
cd linux-stable
```

---

## 3) Configure Kernel (`.config`)

```bash
zcat /proc/config.gz > .config
make O=~/kernels/build olddefconfig
make O=~/kernels/build menuconfig
```

Enable at minimum:

* **Debug/symbols**

  * `CONFIG_DEBUG_INFO=y`
  * `CONFIG_DEBUG_INFO_DWARF5=y`
  * `CONFIG_GDB_SCRIPTS=y`
  * `CONFIG_FRAME_POINTER=y`
  * `CONFIG_KALLSYMS=y`
  * `CONFIG_KALLSYMS_ALL=y`
* **Networking**

  * `CONFIG_INET=y`
  * `CONFIG_PACKET=y`
  * `CONFIG_UNIX=y`
  * `CONFIG_TUN=y`
  * `CONFIG_VIRTIO_NET=y`
* **Filesystems**

  * `CONFIG_EXT4_FS=y`
  * `CONFIG_TMPFS=y`
  * `CONFIG_DEVTMPFS=y`
  * `CONFIG_DEVTMPFS_MOUNT=y`
* **Initrd**

  * `CONFIG_BLK_DEV_INITRD=y`
* **(Optional) KGDB**

  * `CONFIG_KGDB=y`
  * `CONFIG_KGDB_SERIAL_CONSOLE=y`
  * `CONFIG_KDB=y`

Save & exit.

---

## 4) Build Kernel + Modules + GDB Sripts

```bash
make O=~/kernels/build -j"$(nproc)" scripts_gdb bzImage modules
```

Artifacts:

* Kernel: `~/kernels/build/arch/x86/boot/bzImage`
* Symbols: `~/kernels/build/vmlinux`
* Modules (after next step): `~/kernels/rootfs/lib/modules/<version>/`

---

## 5) Install Modules **and Headers**

### 5.1 Install modules to a staging rootfs (for initramfs)

```bash
make O=~/kernels/build modules_install INSTALL_MOD_PATH=~/kernels/rootfs
```

### 5.2 Install **userspace kernel headers** (for building userland against this kernel)

> Not required for boot, but useful if you’ll compile tools or headers-dependent code.

```bash
# Install UAPI headers to a sysroot (kept outside the initramfs)
make O=~/kernels/build headers_install INSTALL_HDR_PATH=~/kernels/sysroot/usr
```

* Headers will land under `~/kernels/sysroot/usr/include/`.

**If you truly need headers inside the initramfs** (rare; increases size):

```bash
mkdir -p ~/kernels/initrd_root/usr
cp -a ~/kernels/sysroot/usr/include ~/kernels/initrd_root/usr/
```

---

## 6) BusyBox initramfs (static)

```bash
cd ~/kernels
git clone https://git.busybox.net/busybox
cd busybox
make defconfig
make menuconfig
# Build Options → [*] Build static binary (no shared libs)
# Networking Utilities → enable the “ip” applet (iproute2)  ← (we use `ip` in init)
# (Optional) Shells → [*] Standalone shell
```

Build **statically with musl** so it truly has no dynamic deps:

```bash
make CC=musl-gcc -j"$(nproc)"
make CONFIG_PREFIX=~/kernels/initrd_root install
```

Also add your kernel modules into the initramfs:

```bash
mkdir -p ~/kernels/initrd_root/lib
cp -a ~/kernels/rootfs/lib/modules ~/kernels/initrd_root/lib/
```

---

## 7) Build a **musl-static zlib** (needed by Dropbear w/ musl)

```bash
cd /tmp
curl -LO https://zlib.net/zlib-1.3.1.tar.xz
tar xf zlib-1.3.1.tar.xz
cd zlib-1.3.1
CC=musl-gcc ./configure --static --prefix=/usr/local/musl
make -j"$(nproc)"
sudo make install
# -> /usr/local/musl/include/zlib.h  and  /usr/local/musl/lib/libz.a
```

---

## 8) Build **Dropbear** (musl, static, pubkey-only)

```bash
cd ~/kernels
git clone https://github.com/mkj/dropbear.git
cd dropbear

# Generate configure if from git
autoreconf -fi

# Clean caches/objs
rm -f config.cache
rm -rf obj dropbearmulti

# Configure: musl, static, no PAM/password/shadow/lastlog
CC=musl-gcc \
CPPFLAGS="-I/usr/local/musl/include" \
LDFLAGS="-L/usr/local/musl/lib" \
CFLAGS="-Os" \
./configure --host="$(uname -m)-linux-musl" \
            --enable-static \
            --disable-pam \
            --disable-shadow \
            --disable-lastlog

# Also enforce pubkey-only in code (belt & suspenders)
cat > localoptions.h <<'EOF'
#define DROPBEAR_SVR_PASSWORD_AUTH 0
#define DROPBEAR_CLI_PASSWORD_AUTH 0
#define DROPBEAR_SVR_PAM_AUTH 0
#define DISABLE_SHADOW
EOF

# Build a single multi-binary that provides dropbear/dropbearkey/scp via argv[0]
make -j"$(nproc)" PROGRAMS="dropbear dropbearkey scp" MULTI=1 CRYPTLIB= LIBS="-lz"
```

### Install Dropbear into the initramfs (**correct multi-binary path + symlinks**)

```bash
mkdir -p ~/kernels/initrd_root/{usr/sbin,usr/bin,etc/dropbear,root/.ssh}

# Put the multi-binary here:
cp ./dropbearmulti ~/kernels/initrd_root/usr/sbin/dropbearmulti

# Create RELATIVE symlinks expected by tools:
ln -sf dropbearmulti         ~/kernels/initrd_root/usr/sbin/dropbear
ln -sf ../sbin/dropbearmulti ~/kernels/initrd_root/usr/bin/dropbearkey
ln -sf ../sbin/dropbearmulti ~/kernels/initrd_root/usr/bin/scp   # optional
```

---

## 9) Init script

```bash
cd ~/kernels/initrd_root
cat > init <<'EOF'
#!/bin/sh
mount -t proc proc /proc
mount -t sysfs sysfs /sys
mount -t devtmpfs devtmpfs /dev
mkdir -p /dev/pts /dev/shm /run /var/run /etc/dropbear /root/.ssh
mount -t devpts devpts /dev/pts

echo "== Initramfs boot =="
mkdir -p /tmp

# Minimal identities (optional but handy)
[ -f /etc/passwd ] || {
  mkdir -p /etc
  echo 'root::0:0:root:/root:/bin/sh' > /etc/passwd
  echo 'root:x:0:' > /etc/group
}
chown -R root:root /root
chmod go-w /root          # or pick one of: chmod 755 /root  (common) or chmod 750 /root or chmod 700 /root


# Network (QEMU usernet default)
ip link set lo up
ip link set eth0 up
ip addr add 10.0.2.15/24 dev eth0
ip route add default via 10.0.2.2

# Dropbear host key (ed25519 is small/fast)
[ -e /etc/dropbear/dropbear_ed25519_host_key ] || \
  /usr/bin/dropbearkey -t ed25519 -f /etc/dropbear/dropbear_ed25519_host_key

# Authorized keys
mkdir -p /root/.ssh
chmod 700 /root/.ssh
echo "ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAACAQCyTCAPVbiKs+05dEIb4Fhkk1YMTZqHOj4TCjV72gjdZnoGW7iejm4dX4lsut5RPutbj6davaasgaK2Zbeffk1ONn96R2IfRZAQ0FIPzHzJG8KSR2KYhrphOw4VibSH/9GvkCCPY9twWPmeBgWCKP4ctRJ+lGstu77NJEAKsgJrWv1Hi/4cVewyOHQlYrLq+gvhKLg02vIMUsUh94U/q/TJlCxwEhglk+001Kl86s/Jih9UPEa59KyotANzUtr/a3imJqW6+vqGOJv6fWGcge4276x+wGrQemfpGXXEWCi+DJ5nt1eKIj3kmHLy9/W2ZeJs6qAPJORQ/aaqEw9CrKBbrf7JeNFCEbfNkyGgxr5yhKhMQiOkVND/87p7kDKT1jhjwFeoqvDhghU58jtCN4HLr8BUwdDSPaCRhCIDxi8OylLQWcka55sdLsovAnMvOzhcquySfoFC9CeRjr85e3l16zBeSk0MdaiZiWIbfm9cIlrh/p/aazrtVcu6MwFVVGX031ET0i7jkRvaTv632eMaDCZGvlQQoU6JCBmaH4LPsO/vZqY10shcKBqAX0acNzsWUfWII+1jHZ8LqKlM/yrmOY9djzbp75tE8zH56cm18qaleJNSnjbFpLuhPB/6oG4SUF7ZJt6BVrCKsffASxhmGeNgaYd8Sx24Usu5CeHQjQ== vdelaplainess@gmail.com" > /root/.ssh/authorized_keys
chmod 600 /root/.ssh/authorized_keys

# Start Dropbear (pubkey-only, log to console)
/usr/sbin/dropbear -R -E -p 22 &
exec /bin/sh
EOF

chmod +x init
```

> Replace the placeholder with your real **public** key.

---

## 10) Build the initramfs

```bash
cd ~/kernels/initrd_root
find . -print0 | cpio --null -ov --format=newc | gzip -9 > ../initrd.cpio.gz
```

---

## 11) Boot with QEMU (SSH port-forward)

```bash
qemu-system-x86_64 \
  -m 2G \
  -kernel ~/kernels/build/arch/x86/boot/bzImage \
  -initrd ~/kernels/initrd.cpio.gz \
  -append "console=ttyS0 root=/dev/ram0 nokaslr" \
  -nographic \
  -netdev user,id=net0,hostfwd=tcp::2222-:22 \
  -device virtio-net-pci,netdev=net0
```

* `-nographic` → serial console in your terminal
* `hostfwd=tcp::2222-:22` → host **2222 → guest 22**

SSH from host:

```bash
ssh -p 2222 root@127.0.0.1
```

---

## 12) (Optional) CLion/GDB kernel debug

Start QEMU paused with a GDB server:

```bash
qemu-system-x86_64 \
  -m 2G \
  -kernel ~/kernels/build/arch/x86/boot/bzImage \
  -initrd ~/kernels/initrd.cpio.gz \
  -append "console=ttyS0 root=/dev/ram0 nokaslr" \
  -s -S -nographic \
  -netdev user,id=net0,hostfwd=tcp::2222-:22 \
  -device virtio-net-pci,netdev=net0
```

In CLion/GDB:

* **Symbols**: `~/kernels/build/vmlinux`
* **Target**: `localhost:1234`
* Then: `b start_kernel`, `c`, etc.

---

## ✅ You now have

* Custom kernel (`bzImage`) + symbol-rich `vmlinux`
* **Modules installed** to a staging rootfs for the initramfs
* **Userspace kernel headers installed** to `~/kernels/sysroot/usr/include`
  (and, if desired, copied into the initramfs at `/usr/include`)
* BusyBox initramfs (musl-static), with modules included
* **musl-static zlib** and **musl-static Dropbear** (pubkey-only)
* Correct **dropbearmulti** placement & relative symlinks:

  * `/usr/sbin/dropbearmulti` (binary)
  * `/usr/sbin/dropbear -> dropbearmulti`
  * `/usr/bin/dropbearkey -> ../sbin/dropbearmulti`
  * `/usr/bin/scp -> ../sbin/dropbearmulti`
* QEMU user networking + SSH port forward
* Optional CLion/GDB setup

If you want, I can hand you a small Makefile that automates sections **5–11** (rebuild initramfs + boot with one command).
