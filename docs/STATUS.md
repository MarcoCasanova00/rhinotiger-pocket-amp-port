# Port status

## Phase 1 — literal baseline (done)

- copied RhinoTiger app, DSP, tests and required base files;
- copied RhinoTiger's own Pocket AMP BSP headers;
- `tools/check_literal.sh` proves the copies are unmodified byte for byte;
- the full host regression suite (40 `tests/*.txt` scripts) passes: `failures: 0`, ended `all passed`.

## Phase 2 — platform skeleton (done)

- `plat/pocketamp/unity.c`: RhinoTiger's unity order for the literal files
  (`libc.c`, `lcd.c`, `gfx.c`, `fm1b_config.h`, `ann_words.h`, `announce.c`,
  `touch.c`, `app.c`), included by `main_pa.c` so everything stays one
  translation unit, with the port's `input.c`, `audio.c`, `midi.c`,
  `store.c`, `plat.c`;
- `plat/pocketamp/main_pa.c`: `fm1_cstart` (load images, `.bss`/`.pool`,
  IRQ init, stock clock and rails, watchdog, flash plain window, board init,
  app boot, main loop) and the `fm1_fault` handler;
- `plat/pocketamp/plat.c`: the `plat_*` contract with safe "not available"
  returns (no USB/Bluetooth/WiFi/CPU2/disk/console/SysEx);
- `plat/pocketamp/store.c`: A/B objects in `0x86000..0xB3FFF` (safe
  `APP_STORE_MAP`), payload at offset 256, 32-byte header written last,
  CRC-32, highest `seq` wins; host-tested (`tools/test_store_host.c`).

## Phase 3 — audio (done)

- `plat/pocketamp/audio.c`: 64-frame ALNK0 halves bridged to 256-frame
  `app_render()` in the ALNK0 interrupt; the input halves are acknowledged and
  discarded (RhinoTiger has no audio input path);
- MASTER read as `plat_master()` (0..4096), passed to `app_render()` as the
  render gain;
- render-load accounting and `app_overload()` shedding.

## Phase 4 — input and MIDI (done)

- `plat/pocketamp/input.c`: virtual-panel translator for the PARA encoder and
  MASTER pot plus the four keys (single keys, chords from the 15 ms-debounced
  stable mask, HOME combos, PARA click/hold focus and save sequences);
- `plat/pocketamp/midi.c`: the literal app's 4-byte packet format, t24
  stamps, and a 16-packet queue (empty until a transport is added);
- all 15 pages reachable through the keys.

## Phase 5 — build and device test (build + emulator done, flash pending)

- `tools/build_pa.py`: pi32v2 compile/link/pack with the local JieLi
  toolchain (no SDK driver source); produces `build/pa/app.bin`
  (text, then `.ram_text` and `.data` at their load addresses);
- `ld/pocketamp.ld`: XIP/RAMTEXT/RAM/POOL/NOINIT layout with `.app_ram`
  folded into `.ram_text`;
- device image builds clean: ~217 KB, `_rt_load`/`_data_load` after the text,
  `.bss` ends before the pool, `.pool` fits `cv_px` + `echo_buf` + `cap_buf`;
- `tools/emu_pa.py`: an interactive emulator of the Pocket AMP panel that
  runs the literal app on the host simulator through the firmware's own
  translator (`plat/pocketamp/pa_virt.c`, shared code); `tools/test_pa_emu.sh`
  regression covers single keys, all six chords, the focus cycle, the KEY
  cursor and the PARA-hold save sequences — all 14 checks pass;
- `tools/emu_pa_server.py`: a localhost browser demo of the port (screen
  stream, live status, lights, audio delivered from the host's WAV recording);
- **flashing path identified**: the app image links at `0x02000120`, the same
  shape as Pendel's (open-source Pocket AMP firmware), whose boot-loader tools
  (`pa_dev.py uboot`, `pa_uboot_dump.py`, `pa_make_image.py`,
  `pa_uboot_write.py`) write the app area only — see `docs/FLASHING.md` and
  `tools/pa_flash_check.sh`;
- **not yet done**: flashing to hardware and the soak test (the silicon paths —
  ALNK0 audio, the ADC, flash writes — need the unit).

## Command

```sh
tools/check_literal.sh     # literal sources byte-for-byte
tools/test_host.sh         # host regression (the app, on the simulator)
tools/build_pa.sh          # (wrapper; see tools/build_pa.py)
python3 tools/build_pa.py  # device image
gcc -I plat/pocketamp -o /tmp/test_store tools/test_store_host.c && /tmp/test_store
```
