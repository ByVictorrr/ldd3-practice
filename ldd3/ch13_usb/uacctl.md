Sun Nov 2, 2025 — America/Los_Angeles

Here’s a complete **Markdown reference page** for your `uacctl` CLI — clean and ready to drop into your project’s README or documentation folder.

---

# 🎧 `uacctl` — USB Audio Control Utility

`uacctl` is a lightweight command-line tool that communicates with your custom USB Audio Class (UAC) kernel driver via `ioctl()` calls.
It provides user-space access to **mute** and **volume** controls for `/dev/uac*` devices.

---

## 🧩 Features

* Toggle **mute** on/off
* Get and set **volume** (in 1/256 dB units)
* Query **minimum** and **maximum** volume limits
* Works directly with `/dev/uac0` without ALSA
* Ideal for testing low-level UAC control requests (`SET_CUR`, `GET_CUR`, etc.)

---

## ⚙️ Build

```bash
gcc -O2 -Wall -Wextra -o uacctl uacctl.c
```

Requires:

* Your kernel-space header `uac_ioctl.h`
* Access to `/dev/uac0` (usually needs `sudo`)

---

## 🧭 Usage

```bash
uacctl [--dev DEVICE] <command> [args]
```
**Start the noise generator**
```bash
sox -n -r 48000 -c 2 -b 16 -e signed-integer -t raw - synth 300 sine 440 \
| sudo dd of=$DEV bs=192 iflag=fullblock status=progress
```
**Default device:** `/dev/uac0`

---

## 🔧 Commands

| Command       | Arguments  | Description                                         |
| ------------- | ---------- | --------------------------------------------------- |
| `get-mute`    | –          | Print mute state (0 = unmuted, 1 = muted)           |
| `set-mute`    | `<0 or 1>` | Set mute off/on                                     |
| `get-vol`     | –          | Print current average volume (L/R)                  |
| `set-vol`     | `<s16>`    | Set volume (in 1/256 dB units; e.g. `-256 = -1 dB`) |
| `get-vol-min` | –          | Print minimum volume (lowest dB)                    |
| `get-vol-max` | –          | Print maximum volume (highest dB)                   |

---

## 💡 Examples

### 🔇 Mute / Unmute

```bash
./uacctl set-mute 1       # Mute
./uacctl get-mute         # → 1
./uacctl set-mute 0       # Unmute
```

### 🔊 Get and Set Volume

```bash
./uacctl get-vol-min      # → -10240
./uacctl get-vol-max      # → 0
./uacctl get-vol          # → -512

./uacctl set-vol -1024    # -4.0 dB
./uacctl set-vol -256     # -1.0 dB
./uacctl set-vol 0        # 0 dB (max)
```

### 🎚️ Fade Volume Loop

```bash
for v in 0 -256 -512 -1024 -2048; do
    echo "volume $v"
    ./uacctl set-vol $v
    sleep 1
done
```

---

## 🔉 Quick Playback Test

Play a 440 Hz tone directly through `/dev/uac0`:

```bash
sox -n -r 48000 -c 2 -b 16 -e signed-integer -t raw - synth 5 sine 440 \
| sudo dd of=/dev/uac0 bs=192 iflag=fullblock status=progress
```

While playing, try:

```bash
./uacctl set-mute 1   # silence
./uacctl set-mute 0   # resume sound
./uacctl set-vol -1024
```

---

## 🧠 Notes

* Volume is represented as **signed 16-bit values** (1 unit = 1/256 dB).

    * `0` → 0 dB (max)
    * `-256` → −1 dB
    * `-1024` → −4 dB
* Mute control applies to **channel 0 (master)**.
* Volume controls apply to **channels 1 & 2 (stereo L/R)**.

---

## 🧑‍💻 Developer Info

| IOCTL Macro           | Description        | Type                |
| --------------------- | ------------------ | ------------------- |
| `UAC_IOC_SET_MUTE`    | Set mute state     | `_IOW('u', 0, int)` |
| `UAC_IOC_GET_MUTE`    | Get mute state     | `_IOR('u', 1, int)` |
| `UAC_IOC_GET_MIN_VOL` | Get minimum volume | `_IOR('u', 2, int)` |
| `UAC_IOC_GET_MAX_VOL` | Get maximum volume | `_IOR('u', 3, int)` |
| `UAC_IOC_SET_VOL`     | Set current volume | `_IOW('u', 4, int)` |
| `UAC_IOC_GET_VOL`     | Get current volume | `_IOR('u', 5, int)` |

---

## 📜 License

`uacctl` is distributed under the **GPLv2** license, consistent with the kernel driver.

---

