---

# 🎯 Problem Statement:

### Implement a Raw USB Audio Streaming Driver for the QEMU `usb-audio` Device

---

## 🧩 **Goal**

Develop a **Linux kernel module** that interfaces directly with the **QEMU `usb-audio` device** using the **USB core API**.
Your driver should:

* Enumerate the audio interface,
* Claim its **isochronous IN endpoint**,
* Continuously capture incoming PCM data, and
* Expose a simple **character device interface** (`/dev/uac0`) that allows user-space to read raw audio samples in real time.

---

## 🧠 **Background**

QEMU’s `usb-audio` emulates a **USB Audio Class (UAC)** compliant device.
When attached to a virtual machine, it presents:

* A **control interface (bInterfaceClass = 0x01)**, and
* An **AudioStreaming interface (bInterfaceClass = 0x01, SubClass = 0x02)** with an **isochronous IN endpoint**.

Isochronous transfers differ from bulk/interrupt transfers:

* They are **time-sensitive** (streaming)
* No retries on error
* Require **URBs with multiple frame descriptors**

Your driver will interact with the kernel’s **USB core**, not ALSA, focusing purely on **USB-level streaming**.

---

## ⚙️ **Requirements**

### 1️⃣ Device Matching and Initialization

* Register a USB driver:

  ```c
  static struct usb_driver uac_driver = {
      .name = "uac_raw",
      .id_table = uac_table,
      .probe = uac_probe,
      .disconnect = uac_disconnect,
  };
  ```
* Match on `USB_DEVICE(<QEMU_VENDOR_ID>, <QEMU_PRODUCT_ID>)` (found via `lsusb` in the guest).
* In `probe()`:

    * Identify the **AudioStreaming interface**.
    * Select the alternate setting that enables the **isochronous IN endpoint**.
    * Allocate buffers and URBs.

---

### 2️⃣ Isochronous Data Streaming

* Use `usb_alloc_urb()` and `usb_fill_iso_urb()`.
* Configure:

    * Transfer type = `USB_ENDPOINT_XFER_ISOC`
    * Flags = `URB_ISO_ASAP`
    * `urb->interval` = endpoint `bInterval`
    * `urb->iso_frame_desc[i].offset/length` per packet
* Submit a **ring of URBs** (e.g., 4–8) to maintain continuous streaming.
* In your URB completion handler:

    * Copy the payload data to a **circular kernel buffer**.
    * Resubmit the URB immediately.

---

### 3️⃣ Character Device Interface

* Create `/dev/uac0` using `usb_class_driver`.
* Implement:

    * `.open()` — verify device presence
    * `.read()` — copy buffered PCM data to user space
    * `.release()` — stop streaming on close

User-space apps can then:

```bash
cat /dev/uac0 > out.raw
aplay -f S16_LE -r48000 -c2 out.raw
```

---

### 4️⃣ Cleanup

* Cancel and free all URBs on `disconnect()`.
* Free buffers and unregister the character device.

---

## 🧪 **Testing Steps**

1️⃣ Launch QEMU:

```bash
qemu-system-x86_64 \
  -M q35 \
  -device qemu-xhci,id=xhci \
  -device usb-audio,bus=xhci.0 \
  -kernel your-bzImage -append "root=/dev/sda" ...
```

2️⃣ Inside the guest:

```bash
lsusb -v | grep -A4 Audio
sudo modprobe -r snd_usb_audio   # avoid conflict
sudo insmod uac_raw.ko
cat /dev/uac0 > capture.raw
aplay -f S16_LE -r48000 -c2 capture.raw
```

You should hear silence or synthetic audio (depending on QEMU’s emulation).

---

## 🧱 **Deliverables**

| File        | Purpose                                 |
| ----------- | --------------------------------------- |
| `uac_raw.c` | Kernel module implementing driver logic |
| `Makefile`  | Builds module against kernel headers    |
| `README.md` | Build, install, and testing steps       |

---

## 🧠 **Learning Outcomes**

You will understand:

* How to locate and claim specific USB interfaces/endpoints
* How **isochronous URBs** differ from bulk/interrupt transfers
* How to build a simple kernel-space **streaming pipeline**
* How to design a ring buffer for real-time data delivery
* How to interact between kernel (URB completions) and user-space (`read()`)

---

## ⚡ **Optional Extensions**

| Extension                  | Description                                                          |
| -------------------------- | -------------------------------------------------------------------- |
| **Asynchronous queueing**  | Implement poll/select support for non-blocking reads                 |
| **Control transfers**      | Use vendor control requests to query device descriptors              |
| **PCM headers**            | Prepend each user-space read with a mini WAV header                  |
| **Stats sysfs**            | Export `/sys/class/uac_raw/stats` (URBs completed, errors, overruns) |
| **Multi-endpoint support** | Capture from multiple interfaces (e.g., stereo)                      |

---
