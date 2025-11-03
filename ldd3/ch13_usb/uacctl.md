---

# 🎧 `uacctl` — USB Audio Control Utility

`uacctl` is a lightweight command-line tool that communicates with your custom **USB Audio Class (UAC)** kernel driver via `ioctl()` calls.
It provides user-space access to **mute** and **volume** controls for `/dev/uac*` devices — no ALSA layer required.

---

## 🧩 Features

* Toggle **mute** (on/off)
* Get and set **volume** in signed **Q8.8 dB units** (1 unit = 1/256 dB)
* Query **minimum**, **maximum**, and **resolution** (step size) values
* Works directly with `/dev/uac0`
* Ideal for testing **UAC Feature Unit** requests such as `SET_CUR`, `GET_CUR`, `GET_MIN`, `GET_MAX`, and `GET_RES`

---

## ⚙️ Build

```bash
gcc -O2 -Wall -Wextra -o uacctl uacctl.c
```

Requirements:

* Kernel-space header: `uac_ioctl.h`
* Access to `/dev/uac0` (use `sudo` if necessary)

---

## 🧭 Usage

```bash
uacctl [--dev DEVICE] <command> [args] [--raw|--db|--pct]
```

**Default device:** `/dev/uac0`

To quickly generate and stream test audio:

```bash
sox -n -r 48000 -c 2 -b 16 -e signed-integer -t raw - synth 300 sine 440 \
| sudo dd of=/dev/uac0 bs=192 iflag=fullblock status=progress
```

---

## 🔧 Commands

| Command       | Arguments  | Description                                   |
| ------------- | ---------- | --------------------------------------------- |
| `get-mute`    | –          | Print mute state (`0` = unmuted, `1` = muted) |
| `set-mute`    | `<0 or 1>` | Set mute off/on                               |
| `get-vol`     | –          | Get current average volume (prints dB and %)  |
| `set-vol`     | `<value>`  | Set volume — accepts Q8.8, dB, or percent     |
| `get-vol-min` | –          | Print minimum volume (lowest dB)              |
| `get-vol-max` | –          | Print maximum volume (highest dB)             |
| `get-vol-res` | –          | Print volume resolution (step size, Q8.8 dB)  |

---

## 💡 Examples

### 🔇 Mute / Unmute

```bash
./uacctl set-mute 1       # Mute
./uacctl get-mute         # → 1
./uacctl set-mute 0       # Unmute
```

### 🔊 Volume Queries

```bash
./uacctl get-vol-min      # → -128.00 dB
./uacctl get-vol-max      # → 0.00 dB
./uacctl get-vol-res      # → 1.00 dB/step
```

### 🎚️ Setting Volume

```bash
./uacctl set-vol -10dB     # set -10 dB
./uacctl set-vol 50%       # halfway between min and max
./uacctl set-vol --raw -2560  # set -10.00 dB using Q8.8 units
```

Result:

```
Applied: -10.00 dB (67.5%)
```

---

## 🔉 Live Test Example

Play a 440 Hz sine wave through your UAC device:

```bash
sox -n -r 48000 -c 2 -b 16 -e signed-integer -t raw - synth 5 sine 440 \
| sudo dd of=/dev/uac0 bs=192 iflag=fullblock status=progress
```

While playing, you can live-adjust controls:

```bash
./uacctl set-mute 1   # Silence
./uacctl set-mute 0   # Resume
./uacctl set-vol -10dB
```

---

## 🧠 Notes

* **Volume format:** signed 16-bit, **Q8.8 fixed-point dB**

  * `0` → 0 dB (max)
  * `-256` → −1 dB
  * `-1024` → −4 dB
* **Mute** applies to **channel 0 (master)**
* **Volume** applies to **channels 1 and 2 (L/R)** and is averaged for `get-vol`
* Values are sign-extended in userspace for accurate negative readings

---

## 🧑‍💻 Developer Reference

| IOCTL Macro           | Description        | Type                |
| --------------------- | ------------------ | ------------------- |
| `UAC_IOC_SET_MUTE`    | Set mute state     | `_IOW('u', 0, int)` |
| `UAC_IOC_GET_MUTE`    | Get mute state     | `_IOR('u', 1, int)` |
| `UAC_IOC_GET_MIN_VOL` | Get minimum volume | `_IOR('u', 2, int)` |
| `UAC_IOC_GET_MAX_VOL` | Get maximum volume | `_IOR('u', 3, int)` |
| `UAC_IOC_GET_RES_VOL` | Get volume step    | `_IOR('u', 6, int)` |
| `UAC_IOC_SET_VOL`     | Set current volume | `_IOW('u', 4, int)` |
| `UAC_IOC_GET_VOL`     | Get current volume | `_IOR('u', 5, int)` |

---

## 📜 License

`uacctl` is distributed under the **GPLv2** license, consistent with the Linux kernel driver.

---

