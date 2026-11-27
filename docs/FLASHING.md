# Flashing the RhinoTiger port to the Pocket AMP

The device image is `build/pa/app.bin`. It is an app image linked at
`0x02000120` — the same shape as Pendel's `pendel.bin` (open-source Pocket AMP
firmware, M-VAVE's FM-1/AC79 family), so the same install paths apply.

## The image

- Flash offset `0x4000` is the app area start, mapped by the SFC at XIP
  `0x02000000`; the SPL/jump to `0x02000120` (our `_start`: `goto +4; rts`).
- `app.bin` is exactly that XIP image: `.text` at its start, then
  `.ram_text` at `_rt_load` and `.data` at `_data_load`, copied by
  `fm1_cstart`.
- Current build (217,088 B, flash `0x4000..0x39000`):

  | part | flash offset | size |
  |---|---|---|
  | `.text` (XIP) | 0x4000 | 0x33C40 |
  | `.ram_text` | 0x37C40 (`_rt_load`) | 0x12AC |
  | `.data` | 0x38EEC (`_data_load`) | 0x114 |
  | store (A/B objects) | 0x86000..0xB3FFF | 4 × 4 KiB sectors |

## Route A — through the chip's boot loader (recommended for this port)

Developed and tested by Pendel (github.com/hugelton/Pendel). Needs Linux, a
USB data cable, `pip install mido python-rtmidi`, and for the last two steps
`jl-uboot-tool` with root. `JL` below = `sudo JLTOOL=/path/to/jl-uboot-tool /path/to/jl-uboot-tool/.venv/bin/python`.

1. Clone Pendel for its tools (dev tools only; nothing of its firmware is used):
   ```sh
   git clone https://github.com/hugelton/Pendel ~/Pendel
   ```
2. Enter the boot loader (works from the stock firmware):
   ```sh
   python3 ~/Pendel/tools/pa_dev.py uboot
   ```
3. Back up the whole flash — keep this file forever (it is the way back to
   the unit's own firmware):
   ```sh
   JL ~/Pendel/tools/pa_uboot_dump.py
   ```
4. Make an image that changes only the app area (boot loader, settings and
   data stay from the backup):
   ```sh
   python3 ~/Pendel/tools/pa_make_image.py BACKUP.bin \
       build/pa/app.bin OUT.bin
   ```
5. Write it (app area only, every sector read back):
   ```sh
   JL ~/Pendel/tools/pa_uboot_write.py --ref BACKUP.bin --image OUT.bin --write
   ```

A crash, a main loop that stops 3 s, or two boots that never reach 2 s drop
the unit back into the boot loader, from where an image can always be written
again.

## Route B — web-installable package (our firmware, from a browser)

The stock firmware speaks the two-step updater, and Pendel's tooling wraps an
app image + the Pendel loader into a `.fwsc` package the updater will install.
Our `app.bin` is the same shape as Pendel's, so it rides that same pipeline:

```sh
tools/pa_pkg.sh          # builds build/pa/pkg/rhinotiger-pa.fwsc
                         # (fetches Pendel's loader from its latest release)
```

Install `build/pa/pkg/rhinotiger-pa.fwsc`:
- **Browser** — Pendel's web installer (Chrome/Edge, WebMIDI): plug in the
  Pocket AMP, choose the file, press Install. The unit restarts twice: once
  into the update loader, once into our firmware.
- **Terminal** — `python3 ~/Pendel/tools/pa_install.py build/pa/pkg/rhinotiger-pa.fwsc --yes`
  (needs `pip install mido python-rtmidi`).

To go back to stock, install the official `Pocket AMP.fwsc` (M-VAVE's
download page) the same way — the installers accept that file by SHA-256.

## Route C — official updater (stock only)

M-UPGRADE / pocket-amp-tools install the official M-VAVE `Pocket AMP.fwsc`
(V105). This only returns the unit to stock.

## First boot checklist

1. Power on → the RhinoTiger logo (犀虎) shows. (Proves boot, clock, LCD.)
2. Keys/chords change pages. (Proves input + the translator.)
3. `VOL−` plays the groove. (Proves ALNK0 audio.)
4. PARA hold 1 s → save, reboot → still set. (Proves the store.)

If something fails, the crash record is in `.noinit` (0x01C7C000..) and the
fatal handler turns the screen red before resetting — same as the FM-1.
