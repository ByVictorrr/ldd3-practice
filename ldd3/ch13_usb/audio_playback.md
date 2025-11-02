## 1) One-liner: generate → feed driver → stream to host (guest side)

```bash
ffmpeg -hide_banner -loglevel error \
  -f lavfi -i "sine=frequency=1000:sample_rate=48000" \
  -f s16le -ac 2 -ar 48000 - \
| tee >(dd of=/dev/uac0 bs=192 status=none) \
| nc 10.0.2.2 5555
```

* `tee` duplicates the 48k S16LE stereo stream:

    * one copy goes through `dd` into **/dev/uac0** in neat 192-byte chunks (1 ms)
    * the other copy goes to the **host** via `nc`

To stop after N seconds, add `:duration=10` back into the `sine=` filter.

## 2) Host: play live or save as WAV

**Play live (Linux):**

```bash
nc -l 5555 | aplay -t raw -f S16_LE -c 2 -r 48000
```

**Save to WAV:**

```bash
nc -l 5555 | sox -t raw -r 48000 -e signed -b 16 -c 2 - capture.wav
```

## 3) Quick sanity checks

* Expected throughput ≈ **192,000 B/s** (48k * 2ch * 2B).
  Watch it with: `pv -brt | dd of=/dev/null` on the host side if you like.
* If host shows nothing: confirm the guest can reach the host (`10.0.2.2`) and that the **host listener is started first**.
* If you hear glitches, your ring is underrunning; bump your ring size or ensure the generator keeps up.

## 4) Tiny quality-of-life scripts

**guest → host (guest script):**

```bash
#!/usr/bin/env bash
set -euo pipefail
HOST_IP=10.0.2.2
PORT=5555
ffmpeg -hide_banner -loglevel error \
  -f lavfi -i "sine=frequency=${1:-1000}:sample_rate=48000" \
  -f s16le -ac 2 -ar 48000 - \
| tee >(dd of=/dev/uac0 bs=192 status=none) \
| nc "$HOST_IP" "$PORT"
```

**host listener (host script):**

```bash
#!/usr/bin/env bash
set -euo pipefail
PORT=5555
nc -l "$PORT" | aplay -t raw -f S16_LE -c 2 -r 48000
# or replace aplay line with: sox -t raw -r 48000 -e signed -b 16 -c 2 - capture.wav
```

---

if you want to stream **anything** (not just a tone), replace the `ffmpeg -f lavfi ...` with:

```bash
ffmpeg -i input.wav -f s16le -ac 2 -ar 48000 -   # or any media file
```

and the rest of the pipeline stays the same.
