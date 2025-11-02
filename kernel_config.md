Sun Nov 2, 2025 · America/Los_Angeles

Absolutely — there are many **useful `CONFIG_*` options** for kernel development, especially when debugging PCIe, I/O, networking, memory, or driver subsystems. Here’s a categorized expansion you can safely merge into your `.config` for **driver development, PCI/IO tracing, and runtime observability**.

---

## 🧠 **General Development Essentials**

```config
CONFIG_KALLSYMS=y
CONFIG_KALLSYMS_ALL=y
CONFIG_IKCONFIG=y
CONFIG_IKCONFIG_PROC=y
CONFIG_STRICT_DEVMEM=n
CONFIG_DEBUG_KERNEL=y
CONFIG_DEBUG_FS=y
CONFIG_DEBUG_INFO=y
CONFIG_DEBUG_INFO_DWARF5=y
CONFIG_DEBUG_INFO_BTF=y
CONFIG_DEBUG_INFO_BTF_MODULES=y
CONFIG_FRAME_POINTER=y
CONFIG_STACKTRACE=y
CONFIG_MAGIC_SYSRQ=y
CONFIG_MAGIC_SYSRQ_DEFAULT_ENABLE=0x1
CONFIG_PROVE_LOCKING=y
CONFIG_DEBUG_SPINLOCK=y
CONFIG_DEBUG_ATOMIC_SLEEP=y
CONFIG_DEBUG_OBJECTS=y
CONFIG_DEBUG_VM=y
CONFIG_DEBUG_MUTEXES=y
CONFIG_LOCK_STAT=y
```

---

## 🔌 **PCI / I/O Debugging**

```config
CONFIG_PCI=y
CONFIG_PCI_MSI=y
CONFIG_PCIEAER=y                 # PCIe Advanced Error Reporting
CONFIG_PCIE_ECRC=y               # End-to-End CRC support
CONFIG_PCIE_DPC=y                # Downstream Port Containment
CONFIG_PCIE_PME=y                # Power Management Events
CONFIG_PCIEPORTBUS=y
CONFIG_PCI_DEBUG=y
CONFIG_PCI_REALLOC_ENABLE_AUTO=y
CONFIG_HOTPLUG_PCI=y
CONFIG_HOTPLUG_PCI_PCIE=y
CONFIG_PCIEASPM=y                # Active State Power Management
CONFIG_PCIEASPM_DEBUG=y
CONFIG_PCIE_BUS_PEER2PEER=y
CONFIG_PCI_IOV=y                 # SR-IOV
CONFIG_PCI_PRI=y
CONFIG_PCI_PASID=y
CONFIG_PCI_ENDPOINT=y
CONFIG_PCI_ENDPOINT_CONFIGFS=y
CONFIG_PCI_ENDPOINT_TEST=m       # /sys/kernel/config/pci_ep/
CONFIG_PCI_EPF_TEST=m
CONFIG_PCI_STUB=m
```

> ✅ **Why:** Enables PCIe advanced error reporting, hotplug debugging, and the `pciehp` driver.
> You’ll also get `/sys/kernel/debug/pcie_err_*` logs and verbose `dmesg` traces from the PCI core.

---

## 🧮 **I/O and DMA Debugging**

```config
CONFIG_DEBUG_DMA_API=y
CONFIG_DMA_API_DEBUG=y
CONFIG_DMADEVICES=y
CONFIG_DMA_ENGINE=y
CONFIG_DMA_CMA=y
CONFIG_CMA_DEBUG=y
CONFIG_IOMMU_DEBUGFS=y
CONFIG_IOMMU_SUPPORT=y
CONFIG_IOMMU_DEFAULT_PASSTHROUGH=y
CONFIG_SWIOTLB=y
CONFIG_SWIOTLB_DYNAMIC=y
CONFIG_BUG_ON_DATA_CORRUPTION=y
CONFIG_SLUB_DEBUG_ON=y
```

> ✅ **Why:** Ensures DMA mapping correctness and reports invalid or double-unmap calls.

---

## 🌐 **Networking & Netdev Debugging**

```config
CONFIG_NET=y
CONFIG_INET=y
CONFIG_PACKET=y
CONFIG_UNIX=y
CONFIG_NETFILTER=y
CONFIG_NET_SCH_FQ_CODEL=y
CONFIG_NET_SCHED=y
CONFIG_NET_CLS_BASIC=y
CONFIG_NET_CLS_U32=y
CONFIG_NET_CLS_ACT=y
CONFIG_NET_DEVLINK=y
CONFIG_NET_RX_BUSY_POLL=y
CONFIG_NET_POLL_CONTROLLER=y
CONFIG_NET_DEV_STATS=y
CONFIG_NET_NS=y
CONFIG_NET_SELFTESTS=y
CONFIG_NET_DROP_MONITOR=y
CONFIG_NET_PKTGEN=m              # kernel packet generator
CONFIG_NETFILTER_XT_TARGET_LOG=m
CONFIG_NETFILTER_XT_MATCH_CONNTRACK=m
CONFIG_TCP_CONG_BBR=y
CONFIG_NET_SOCK_MSG=y
CONFIG_TUN=m
CONFIG_VETH=m
CONFIG_BRIDGE=m
CONFIG_VLAN_8021Q=m
CONFIG_BONDING=m
CONFIG_MACVLAN=m
CONFIG_DUMMY=m
CONFIG_IFB=m
CONFIG_NETCONSOLE=m
CONFIG_NETPOLL_TRAP=y
CONFIG_NETPOLL=y
CONFIG_NET_TCPPROBE=m
```

> ✅ **Why:** Enables `netconsole`, `pktgen`, and net tracing via `/sys/kernel/debug/tracing/events/net/`.
> Super useful for diagnosing NIC drivers, MTU bugs, or TX/RX queue starvation.

---

## 💽 **Block & Storage Debugging**

```config
CONFIG_BLK_DEV_LOOP=y
CONFIG_BLK_DEV_RAM=y
CONFIG_BLK_DEV_NULL_BLK=m
CONFIG_BLK_DEV_NVME=m
CONFIG_BLK_DEV_SD=y
CONFIG_BLK_DEV_INITRD=y
CONFIG_BLK_DEV_IO_TRACE=y
CONFIG_DEBUG_BLOCK_EXT_DEVT=y
CONFIG_BLK_DEBUG_FS=y
```

---

## 🧍‍♂️ **Memory / Page / Slab Debugging**

```config
CONFIG_DEBUG_PAGEALLOC=y
CONFIG_PAGE_POISONING=y
CONFIG_PAGE_POISONING_NO_SANITY=y
CONFIG_PAGE_POISONING_ZERO=y
CONFIG_SLUB_DEBUG=y
CONFIG_PAGE_EXTENSION=y
CONFIG_DEBUG_MEMORY_INIT=y
CONFIG_PAGE_OWNER=y
CONFIG_MEMCG=y
CONFIG_MEMCG_DEBUG=y
```

> ✅ **Why:** Helps catch double-free, use-after-free, or leaking pages.

---

## ⚙️ **Tracing & Instrumentation**

```config
CONFIG_FTRACE=y
CONFIG_FUNCTION_TRACER=y
CONFIG_FUNCTION_GRAPH_TRACER=y
CONFIG_KPROBES=y
CONFIG_KPROBE_EVENTS=y
CONFIG_UPROBES=y
CONFIG_DYNAMIC_EVENTS=y
CONFIG_EVENT_TRACING=y
CONFIG_TRACEPOINTS=y
CONFIG_STACK_TRACER=y
CONFIG_BLK_DEV_IO_TRACE=y
CONFIG_BPF=y
CONFIG_BPF_SYSCALL=y
CONFIG_BPF_JIT=y
CONFIG_BPF_EVENTS=y
CONFIG_PERF_EVENTS=y
CONFIG_PROBE_EVENTS_BTF_ARGS=y
```

> ✅ **Why:** Enables `ftrace`, `tracefs`, `perf`, `bpftrace`, `bpf_probe_read_kernel()` etc.
> Especially helpful for live-debugging device driver paths without recompiling.

---

## 🔊 **Sound/USB/Device Testing**

```config
CONFIG_USB_MON=y
CONFIG_SND_USB_AUDIO=m
CONFIG_SND_DEBUG=y
CONFIG_SND_DEBUG_VERBOSE=y
CONFIG_SND_PCM_XRUN_DEBUG=y
CONFIG_SND_PCM_TIMER=y
CONFIG_SOUND_PRIME=y
CONFIG_HIDRAW=y
CONFIG_INPUT_EVDEV=y
```

> ✅ **Why:** USB monitor (`cat /sys/kernel/debug/usb/usbmon/0u`) gives raw USB traffic; ALSA debug logs show control flow for your UAC driver.

---

## 🧰 **Developer Quality of Life**

```config
CONFIG_PSTORE=y
CONFIG_PSTORE_RAM=y
CONFIG_PSTORE_CONSOLE=y
CONFIG_PSTORE_PMSG=y
CONFIG_PSTORE_FTRACE=y
CONFIG_PRINTK_TIME=y
CONFIG_PRINTK_CALLER=y
CONFIG_BOOT_PRINTK_DELAY=y
CONFIG_BOOTPARAM_HUNG_TASK_PANIC=y
CONFIG_BOOTPARAM_SOFTLOCKUP_PANIC=y
CONFIG_DYNAMIC_DEBUG=y
CONFIG_EARLY_PRINTK=y
CONFIG_EARLY_PRINTK_DBGP=y
CONFIG_EARLY_PRINTK_USB=y
CONFIG_STACKDEPOT=y
CONFIG_PROFILING=y
CONFIG_FUNCTION_PROFILER=y
CONFIG_PROVE_RCU=y
```

---

## ✅ **Apply It**

You can add these to your `dev-kernel.config` fragment, or merge them in via:

```bash
./scripts/kconfig/merge_config.sh -m O="$HOME/kernels/build" \
    "$HOME/kernels/build/.config" dev-kernel.config extra-dev.config
make O="$HOME/kernels/build" olddefconfig
```

---

If you’d like, I can generate a **single ready `.config`** that merges:

* `x86_64_defconfig`
* all of your requested developer/debug/net/PCI options
  …so you can drop it straight into `$KBUILD_OUTPUT/.config` and start compiling.

Would you like me to generate that full merged `.config` file?
