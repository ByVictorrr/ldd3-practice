# QEMU USB-Audio (class-compliant sink)

**Device identity**

* **Vendor ID / Product ID:** `0x46f4 : 0x0002` (0x46f4 is CRC16("QEMU")). ([chromium.googlesource.com][1])
* **Strings:** Manufacturer “QEMU”, Product “QEMU USB Audio”, Serial “1”. ([chromium.googlesource.com][1])
* **USB version (bcdUSB):** `0x0100` (USB 1.1). ([chromium.googlesource.com][1])

**Interfaces & class**

* **Interface 0:** Audio Control (AC), class `0x01`, subclass `0x01`. ([chromium.googlesource.com][1])
* **Interface 1:** Audio Streaming (AS), class `0x01`, subclass `0x02`, with multiple **alternate settings**:

  * **Alt 0:** stream off (no endpoints).
  * **Alt 1:** stream on (see endpoint below).
  * Optional multichannel alts (**5.1** and **7.1**) when the device is created in “multi” mode. ([chromium.googlesource.com][1])

**Streaming format (Alt 1, stereo)**

* **Type I PCM**, **2 channels**, **16-bit**, **48 kHz** fixed.
  (Format Type descriptor: bSubFrameSize=2, bBitResolution=16, tSamFreq=48000). ([chromium.googlesource.com][1])
* **Isochronous endpoint (playback sink):**

  * **Direction:** **OUT** (host → device) at address `0x01`.
  * **bmAttributes:** `0x0D` → Isochronous, **Adaptive**, data endpoint.
  * **bInterval:** 1 (1 ms).
  * **wMaxPacketSize (stereo):** 192 bytes (48 samples × 2 ch × 2 bytes @ 1 ms = 48 kHz). ([chromium.googlesource.com][1])

**Multichannel variant (if enabled)**

* Same 48 kHz / 16-bit Type-I PCM, with **6-ch (5.1)** and **8-ch (7.1)** alternate settings; packet size scales with channel count. ([chromium.googlesource.com][1])

**Descriptor notes**

* The AC interface has Input Terminal, Feature Unit (volume), and Output Terminal descriptors sized for the stream topology the device exposes. ([chromium.googlesource.com][1])
* Some QEMU versions used an **invalid AC bInterfaceProtocol value**; there was a patch series to fix this so hosts like macOS enumerate it as an Audio device properly. If you see enumeration quirks on older QEMU, that’s likely why. ([GNU Lists][2])

**How to add it in QEMU**

```bash
qemu-system-x86_64 \
  -device qemu-xhci,id=xhci \
  -device usb-audio,bus=xhci.0
```

(“usb-audio” is listed among QEMU’s available USB devices.) ([qemu.org][3])

**Relevant class specs (for deeper parsing/testing)**

* **USB Audio Class 1.0** (legacy but simple). ([usb.org][4])
* **USB Audio Class 2.0 (latest doc set, Apr 2025)** — endpoint attributes, control selectors, etc. ([usb.org][5])

---

## Practical implication for your project (Option A)

Because QEMU’s device is a **playback sink** (isochronous **OUT**), your raw USB driver should **send** PCM to the device (queue **iso OUT URBs**). If you specifically want to **capture** (iso **IN**), you’ll need a different device (e.g., a real UAC microphone via passthrough, or simulate a capture device with Linux’s dummy_hcd/Raw Gadget).

If you want, I can tweak the problem statement you’re using so it targets **iso OUT** (playback) with a ring of isochronous **OUT** URBs and a simple userspace feeder that streams a WAV/RAW buffer into `/dev/uac0`.

[1]: https://chromium.googlesource.com/external/qemu/%2B/master/hw/usb/dev-audio.c "hw/usb/dev-audio.c - external/qemu - Git at Google"
[2]: https://lists.gnu.org/archive/html/qemu-trivial/2024-03/msg00117.html?utm_source=chatgpt.com "Re: [PATCH] usb-audio: Fix invalid values in AudioControl descriptors"
[3]: https://www.qemu.org/docs/master/system/devices/usb.html "USB emulation — QEMU  documentation"
[4]: https://www.usb.org/sites/default/files/audio10.pdf?utm_source=chatgpt.com "Audio10.PDF - USB-IF"
[5]: https://www.usb.org/sites/default/files/Audio2_with_Errata_and_ECN_through_Apr_2_2025.pdf?utm_source=chatgpt.com "Universal Serial Bus Device Class Definition for Audio Devices - USB-IF"
