# Raspberry Pi MCP3008 Device Tree Overlay – README

This guide shows how to write, compile, and load a Device Tree **overlay** for an MCP3008 ADC on Raspberry Pi. It covers both **runtime loading** (no reboot) and **boot-time** loading (persistent).

> Works for Pi 3/4/5. Paths differ slightly by distro: some use `/boot/firmware`, others `/boot`.

---

## 1) Prerequisites

* Raspberry Pi OS/Debian with kernel headers and device tree compiler:

```bash
sudo apt update
sudo apt install device-tree-compiler
```

* SPI enabled (one-time):

```bash
# Option A: raspi-config
sudo raspi-config  # Interfacing Options → SPI → Enable

# Option B: config.txt (reboot required)
# Add one of these lines to /boot/firmware/config.txt or /boot/config.txt
# dtparam=spi=on
```

* Confirm `&spi0` (or `&spi1`) exists in your active DT:

```bash
grep -R "spi@" /proc/device-tree 2>/dev/null | head
```

---

## 2) Example overlay source (mcp3008.dts)

Save this as `mcp3008.dts`:

```dts
/dts-v1/;
/plugin/;

/ {
    compatible = "brcm,brcm2835";
    model = "Raspberry Pi 5";

    fragment@0 {
        target = <&spi0>;            // change to &spi1 if you wired to SPI1
        __overlay__ {
            #address-cells = <1>;
            #size-cells = <0>;

            adc@0 {                   // unit address must match reg below
                compatible = "tutorial,mcp3008"; // standard compatible
                reg = <0>;            // chip select 0 (use <1> for CS1)
                spi-max-frequency = <1000000>; // 1 MHz
                // spi-cpol;          // uncomment if device needs mode 2/3
                // spi-cpha;
            };
        };
    };
};
```

**Notes**

* `adc@0` **must** match `reg = <0>` (chip select). If you wire to CS1, use `adc@1` + `reg = <1>`.
* If your distro exposes SPI1 headers/HATs, switch `target = <&spi0>;` → `target = <&spi1>;`.

---

## 3) Compile the overlay

Always compile overlays with symbols (`-@`) and to a `.dtbo` binary file:

```bash
# Compile to mcp3008.dtbo
dtc -@ -I dts -O dtb -o mcp3008-tutorial.dtbo mcp3008-tutorial.dts
```

If you accidentally run `dtc mcp3008.dts` without `-o`, it will dump binary to the terminal; run `reset` to recover the shell.

---

## 4) Load at runtime (no reboot)

Use the Raspberry Pi firmware helper:

```bash
# Load in the current session (does not persist across reboots)
sudo dtoverlay ./mcp3008-tutorial.dtbo

# List loaded overlays
sudo dtoverlay -l

# Remove the most recently added overlay (or use an index from -l)
sudo dtoverlay -r 0
```

**Verify** the node is present:

```bash
hexdump -C /proc/device-tree/*spi@*/adc@0/compatible
# expected to contain: microchip,mcp3008
```

If you’re also exposing a userspace SPI device, you may see `/dev/spidev0.0` (or `0.1`). That’s separate from this overlay and depends on `dtoverlay=spi0-0cs`/`spi0-1cs` or the default spidev policy.

---

## 5) Load at boot (persistent)

### Option A: Place overlay file and reference by name

1. Copy your `.dtbo` into the firmware overlays directory:

```bash
# Pick the one that exists on your system
sudo cp mcp3008-tutorial.dtbo /boot/overlays/
# or
sudo cp mcp3008-tutorial.dtbo /boot/firmware/overlays/
```

2. Append to your firmware config file:

```ini
# /boot/config.txt  OR  /boot/firmware/config.txt

dtoverlay=mcp3008-tutorial
# ensure SPI controller is enabled
dtparam=spi=on
```

3. Reboot:

```bash
sudo reboot
```

### Option B: Absolute path (if supported by your firmware)

Some firmware builds accept absolute/relative paths:

```ini
# Not universally supported, prefer Option A when possible
dtoverlay=mcp3008-tutorial
```

---
### Blacklist mcp320x
```bash
echo -e "blacklist mcp320x\nblacklist spidev" | sudo tee /etc/modprobe.d/zz-local-blacklist.conf

```

## 6) Troubleshooting

**Warning: `SPI bus unit address format error, expected "0"`**

* Your node name didn’t match `reg`. Use `adc@0` when `reg = <0>`.

**`fragment` misspelled**

* Must be `fragment@N`, not `fragement@N`.

**`target = <&spi0>` not found**

* Make sure SPI is enabled (`dtparam=spi=on`) and that your platform actually provides `&spi0` (use `&spi1` otherwise). Inspect live DT under `/proc/device-tree/`.

**Overlay loads but device driver doesn’t bind**

* Ensure `compatible = "microchip,mcp3008"` (or a string your driver supports).
* Use `dmesg | grep -i mcp3008` and `lsmod` to verify your driver.

**Need a specific SPI mode (CPOL/CPHA)**

* Uncomment `spi-cpol;` and/or `spi-cpha;` to request modes 2/3 accordingly.

**No `/dev/spidevX.Y`**

* Spidev nodes are created by separate overlays/policies; this MCP3008 overlay describes the ADC to kernel drivers, not spidev. If you need raw spidev, enable an appropriate spidev overlay.

---

## 7) Quick checklist

* [ ] SPI enabled (`dtparam=spi=on`)
* [ ] `mcp3008.dts` compiled with `dtc -@ -O dtb -o mcp3008.dtbo`
* [ ] Runtime test: `sudo dtoverlay ./mcp3008.dtbo` and verify under `/proc/device-tree`
* [ ] Persistent boot: copy to `/boot*/overlays/` and add `dtoverlay=mcp3008` in `config.txt`

---

## 8) Appendix: Minimal overlay template

```dts
/dts-v1/;
/plugin/;

/ {
    fragment@0 {
        target = <&spi0>;            // or &spi1
        __overlay__ {
            #address-cells = <1>;
            #size-cells = <0>;

            device@CS {
                compatible = "vendor,device";
                reg = <CS>;          // 0 or 1 typically on Pi
                spi-max-frequency = <1000000>;
            };
        };
    };
};
```

Happy hacking! If you want, we can add a testing section with a small C snippet to read a channel from the MCP3008 over SPI to confirm end‑to‑end.
