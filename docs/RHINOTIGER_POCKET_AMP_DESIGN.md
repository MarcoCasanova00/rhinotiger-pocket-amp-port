# RhinoTiger → Pocket AMP: literal-source port design

**Status:** design only; no firmware source files modified.  
**Date:** 2026-11-27  
**Ported code:** `hacha/rhinotiger` (`app/`, `app/dsp/`, selected `base/` headers/libs).  
**Hardware reference:** `28spawn/Andromeda` and RhinoTiger’s own `base/bsp/boards/pocketamp/` headers may be used as *specification only*. **No Andromeda source is copied, compiled, linked or included.**

---

## 1. Goal and decision

Port **RhinoTiger’s literal source** to the M-VAVE/Donner Pocket AMP:

- keep `app.c`, the DSP, the pages, the AUTO PLAY logic, the screen drawing and the settings logic unchanged;
- replace only the FM-1 platform with a new Pocket AMP platform written in RhinoTiger’s `plat_*` style;
- expose the whole instrument through **two knobs and four musical keys**, with `HOME` and `PARA` click/hold as system modifiers;
- use the locally available JieLi toolchain for real device builds and verification.

The port is therefore:

1. literal RhinoTiger app/DSP/library files;
2. a new `plat/pocketamp/` layer implementing RhinoTiger’s `plat.h` contract;
3. a new linker script, unity build and packer;
4. a virtual-panel translator that maps Pocket AMP controls to the FM-1 controls `app.c` expects.

---

## 2. Literal source boundary

These files are copied verbatim and must show **no diff** against the upstream checkout:

| Literal file | Role |
|---|---|
| `app/app.h`, `app/app.c` | Contract, 15 pages, input, MIDI, save logic, host hooks |
| `app/face.h`, `app/logo.h`, `app/auto.h`, `app/hands.h` | Boot art and AUTO PLAY |
| `app/dsp/psy.h` | Public engine API |
| `app/dsp/psy.c` | Engine; `#include`s `events.c`, `fx.c`, `line.c`, `hands.c` |
| `app/dsp/events.c`, `fx.c`, `line.c`, `hands.c` | Included fragments of `psy.c`, **not separate translation units** |
| `base/include/plat.h`, `base/include/fm1b_config.h`, `base/include/fastmath.h` | Platform/config/math contracts |
| `base/device/gfx.c` | Canvas renderer and proportional text |
| `base/lib/touch.c`, `base/lib/touch.h`, `base/device/announce.c` | Held-key/velocity and screen-reader glue used by `app.c` |
| `base/bsp/boards/pocketamp/pa_audio.h`, `pa_input.h`, `pa_lcd_hw.h`, `pa_lcd_init.h` | RhinoTiger’s own Pocket AMP hardware contracts |
| `base/bsp/chip/*.h`, `base/bsp/chip/crt0.S` | JieLi AC79 chip-level register/IRQ/GPIO/timer contracts those headers include |
| generated `felucca_font.h` from `base/tools/gen_font.py` | Font for `gfx.c` |

`app.c` is a **unity-build** source: it expects `gfx.c`, `announce.c` and `touch.c` to be in the same translation unit before it. The new `plat/pocketamp/unity.c` preserves that order:

```c
#include "plat.h"
#include "app.h"
#include "touch.h"
#include "gfx.c"
#include "announce.c"
#include "touch.c"
#include "app.c"
#include "main_pa.c"
```

`psy.c` remains a separate `-O2` unit. No edits to the literal files are planned; only compile-time defines such as `APP_CANVAS_PX` may be supplied.

**Acceptance:** `diff -ru` of the literal directories against upstream is empty. All porting code lives under `plat/pocketamp/`, `ld/`, `tools/` and `docs/`.

---

## 3. Pocket AMP hardware contract

RhinoTiger’s `base/bsp/boards/pocketamp/` headers already describe the target hardware; those headers are used literally. No Andromeda implementation is needed.

| Item | Contract |
|---|---|
| MCU | JieLi AC791N, pi32v2, 360 MHz |
| Toolchain | `JIELI_TOOLCHAIN=$HOME/.jieli/toolchain` → `jieli-linux-toolchains-20260730.1`; `clang` in `common/bin/` |
| SDK | `AC79_SDK=$HOME/fw-AC79_AIoT_SDK` |
| Display | 240×240 ST7789-class SPI1, RGB565 big-endian; CS PB1, D/C PB5, PB6/PB7, backlight PA9 active low |
| Audio | ALNK0 I2S full duplex: PA4 MCLK, PA5 SCLK, PA6 LRCK, PA7 out, PA8 in; 44.1 kHz, 24-bit |
| Audio block | 64 frames per DMA half; RhinoTiger renders 256 frames (`APP_BLOCK`) |
| Sample format | 24-bit right-aligned in `int32_t`; input not sign-extended: `(int32_t)(x << 8) >> 8` |
| Buttons | PH6 `PARA` push, PH8 `HOME`, PH9 `VOL−`, PA0 `VOL+`, PA1 `<`, PA3 `>`; active low |
| Encoder | PC0/PC1, pull-ups enabled by the application |
| Flash | 1 MB SPI-NOR; firmware writes only `0x86000..0xB3FFF`; OTA staging `0xB8000..0xBCFFF` |
| Timer | 24 MHz free-running tick for MIDI timestamps and CPU measurement |

---

## 4. Control translation: two knobs + four keys → literal RhinoTiger panel

`app.c` expects:

- `plat_buttons()` — 14 held virtual buttons (`B_FX`, `B_SEL`, `B_ENV`, `B_LFO`, `B_EDIT`, `B_GLO`, `B_HOME`, `B_SAVE`, `B_ARP`, `B_SEQ`, `B_PLAY`, `B_REC`, `B_OCTDN`, `B_OCTUP`);
- `plat_enc(role)` — `EN_SELECT`, `EN_PRESET`, `EN_ALGO`, `EN_K1..EN_K4`;
- `plat_keys()` — 27 held virtual keys;
- `plat_master()` — MASTER position `0..4096`.

The translator synthesizes all of those. `app.c` keeps its own hold logic (500 ms for ARP/REC/LFO, 1500 ms for GLOBAL+SAVE); the platform shapes the virtual held mask so those timings still apply.

### 4.1 Two knobs

`PARA` click cycles a visible focus:

| Focus | `PARA` turn emits | `MASTER` delta emits |
|---|---|---|
| 0 PAGE | `EN_SELECT` | nothing |
| 1 WORLD | `EN_PRESET` | `EN_PRESET` |
| 2..5 PARAM | `EN_K1..EN_K4` | same focus knob (`EN_K1..EN_K4`) |
| 6 ALGO | `EN_ALGO` | nothing |
| 7 KEY | moves virtual-key cursor | nothing |

`MASTER` is always read by the platform as `plat_master()` and passed to `app_render()` as `APP_GAIN(master_pos)` (`m * m >> 12`, from `fm1b_config.h`). Its **delta** is additionally emitted as the second encoder only in the focuses shown above.

### 4.2 Four musical keys — single gestures

Single-key gestures map to performance buttons; combos suppress the individual mapping.

| Gesture | Virtual button | Literal `app.c` behavior |
|---|---|---|
| `VOL−` tap/hold | `B_PLAY` | play/rest; tap during logo starts playback |
| `VOL+` tap | `B_ARP` | build |
| `VOL+` hold 500 ms | `B_ARP` held | drop |
| `<` tap | `B_SAVE` | break cue; save while GLOBAL is shown |
| `<` hold 1500 ms on GLOBAL | `B_SAVE` held | restore defaults |
| `>` tap | `B_REC` | arm/take |
| `>` hold 500 ms | `B_REC` held | clear |

### 4.3 Page and modifier chords

These reach all remaining page buttons. Chords are recognized from the 15 ms-debounced stable mask; the component keys are not emitted separately.

| Chord | Virtual button | Page(s) |
|---|---|---|
| `VOL−` + `VOL+` | `B_HOME` | MAIN / GROOVE |
| `<` + `>` | `B_SEQ` | BREAK / BUILD / AUTO |
| `VOL−` + `<` | `B_SEL` | KICK / BASS |
| `VOL+` + `>` | `B_ENV` | PERC / EVENTS |
| `VOL−` + `>` | `B_LFO` | LINE / LINE SEQ / ATMOS |
| `VOL+` + `<` | `B_EDIT` | MIX |
| `HOME` + `VOL−` | `B_GLO` | GLOBAL |
| `HOME` + `VOL+` | `B_FX` | ECHO |
| `HOME` + `<` / `HOME` + `>` | `B_OCTDN` / `B_OCTUP` | throw-key octave |
| `PARA` hold 1 s | `B_GLO` then `B_SAVE` tap | save settings |
| `PARA` hold 3 s | `B_GLO` then `B_SAVE` held 1.6 s | factory defaults |

`HOME` alone is the system modifier; `PARA` click/hold is select/save. They are not counted as the four musical keys.

### 4.4 Virtual keys and FX

In focus 7:

- `PARA` turn or `<`/`>` moves the virtual-key cursor `0..26`;
- `VOL−` sets that bit in `plat_keys()` while held; literal `key_down()`/`key_up()` and `touch.c` handle velocity/pressure with `plat_sense(0)`;
- `VOL+` applies the alternate action: FX slot on ECHO, pressure/alt for event keys;
- long `VOL−` invokes black-key performance actions (mute, fill, half, gap, part out);
- long `VOL+` clears FX, notes and mutes.

### 4.5 Lights

`plat_lights()`, `plat_lights_mid()`, `plat_leds()`, `plat_glow()` and `plat_play_red()` are no-ops or backlight-only. The Pocket AMP has no per-key/per-button LEDs; the literal screen draws the same state.

---

## 5. New platform layer

```text
pocketamp-rhinotiger/
  app/                 literal RhinoTiger app files
  app/dsp/             literal RhinoTiger DSP files (upstream layout)
  base/                literal headers/libs/tools/fonts used by them
  plat/pocketamp/
    unity.c            unity order for gfx/announce/touch/app
    plat.c             plat_* functions required by base/include/plat.h
    input.c            PA button/encoder/ADC scan + virtual-panel translator
    audio.c            ALNK0 I2S + 64↔256 frame ring + app_render bridge
    lcd.c              lcd_sync / lcd_fill / lcd_blit over pa_lcd_hw.h
    store.c            A/B object store in the permitted flash window
    midi.c             USB MIDI bytes → RhinoTiger 4-byte packets
    main_pa.c          boot, app_init, 60 Hz app_frame loop, audio start
    platform.h         project-local constants
  ld/pocketamp.ld      .pool / .ram_text / .data / .bss layout
  tools/build_pa.py    pi32v2 compile/link/package
  docs/                physical control card and porting notes
```

### 5.1 `plat_*` groups

`plat.c` implements every declaration in `base/include/plat.h`, including unused FM-1 facilities, with documented “not available” returns:

- time/CPU: `plat_ms`, `plat_ticks24`, `plat_lock`, `plat_unlock`, `plat_yield`, `plat_cpu_pct`, `plat_cpu_khz`, `plat_xruns`;
- panel: `plat_buttons`, `plat_keys`, `plat_enc`, `plat_master`;
- battery/headphones: `plat_battery`, `plat_battery_raw`, `plat_headphones`;
- lights: all `plat_lights*`, `plat_leds`, `plat_glow`, `plat_play_red`, `plat_dim_ns`;
- MIDI/USB/BLE: `plat_midi_*`, `plat_usb_*`, `plat_ble_*`, `plat_bta_*` (BLE disabled by default);
- storage: `plat_store_load/head/size/save/room`;
- console/SysEx/host/disk: stubs where the Pocket AMP build does not expose them;
- identity/restart/log: `plat_chip_uid`, `plat_restart`, `plat_log`;
- second core/cache/RAM-code: `plat_cpu2_*`, `plat_code_window`, `plat_core` (single-core defaults).

### 5.2 Drawing

`gfx.c` calls `lcd_sync()`, `lcd_fill()` and `lcd_blit()`. `lcd.c` implements those from scratch using the literal `pa_lcd_hw.h` / `pa_lcd_init.h` contracts:

- SPI1 setup and panel init from `PA_LCD_INIT[]`, MADCTL `0x60`;
- `lcd_fill()` via a small line buffer;
- `lcd_blit()` via SPI DMA, preserving `gfx.c`’s byte-swapped RGB565 convention;
- `lcd_sync()` waits for the DMA-complete flag before `cv_px` is touched again.

No Andromeda font/UI/display code is used.

### 5.3 Audio bridge

Literal contract:

```c
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);
/* lr[2*i], lr[2*i+1] are signed 24-bit left-aligned in int32_t, n = APP_BLOCK = 256 */
```

Bridge behavior:

1. `pa_audio_init()` owns 64-frame out/in halves (`pa_audio.h`).
2. The ISR acknowledges input and calls `pa_audio_free_half()`; input is sign-corrected if read: `(int32_t)(x << 8) >> 8`.
3. `audio.c` keeps a 256-frame output ring. When it needs a new block it calls `app_render(block, 256, APP_GAIN(plat_master()))`.
4. The ring emits 64-frame TX halves; samples are packed as right-aligned signed 24-bit for ALNK0.
5. `app_render()` owns gain glide, limiting and soft clipping; no second gain stage is added.
6. A silent block primes the ring at boot; the ISR never draws, allocates or touches flash.
7. `app_panic()` is called before any flash rewrite or OTA commit.

### 5.4 Boot order

`main_pa.c`:

1. clocks, watchdog, P33, 24 MHz timer, GPIO, ADC, LCD, USB, ALNK setup;
2. read MASTER four times before audio, as RhinoTiger’s `main_fm1.c` does, and set `master_pos`;
3. `app_init()` — literal engine and settings restore;
4. `app_ui_init()` — literal logo/first screen;
5. start ALNK I2S;
6. main loop: `pa_input_poll()`, virtual-panel scan, `app_frame()` at ~60 Hz, USB MIDI poll, watchdog feed, deferred flash writes.

### 5.5 Persistence

`base/device/storage.c` is **not compiled**: its `APP_STORE_MAP` is FM-1-specific (`0xFC000`, `0xDC000`). The new `store.c` keeps the same object semantics but uses Pocket AMP-safe addresses:

| Object | Sector A | Sector B |
|---|---|---|
| `OBJ_SETTINGS` | `0x00086000` | `0x00087000` |
| `OBJ_PANEL` | `0x00088000` | `0x00089000` |

Record format matches RhinoTiger: 4 KiB sectors, 32-byte header at offset 0, payload at offset 256, zlib CRC-32, `seq`, highest valid copy wins, header written last. Saves are deferred ~2 s after the last change and flushed before OTA/flash rewrites.

### 5.6 MIDI

`midi.c` turns USB MIDI bytes into the 4-byte packets `plat.h` specifies:

```c
uint32_t cin = status >= 0xF0 ? 0x0F : (uint32_t)status >> 4;
uint32_t pkt = cin | ((uint32_t)status << 8) | ((uint32_t)d1 << 16) | ((uint32_t)d2 << 24);
```

`plat_midi_in_t24()` returns `plat_ticks24()` from the 24 MHz timer. Literal `midi_poll()` then handles clock, start/continue/stop and notes unchanged. BLE MIDI is optional and disabled by default.

---

## 6. Function preservation

| RhinoTiger feature | How it is reached |
|---|---|
| 15 pages | `EN_SELECT` focus or page chords in §4.3 |
| 55 parameters | `EN_K1..EN_K4` + `MASTER` delta |
| 8 worlds | `EN_PRESET` focus |
| 4 macros | MAIN page, `EN_K1..K4` |
| 16 FX | ECHO virtual-key grid, focus 7 |
| 27 throw notes | virtual-key cursor, focus 7 |
| mutes / part out / fill / half / gap / all-back | ECHO black-key emulation |
| break / build / drop | literal `B_SAVE`, `B_ARP`, `B_PLAY` behavior |
| REC loop | literal `B_REC` behavior |
| AUTO PLAY | literal AUTO page and `auto.h` |
| LINE new phrase | literal `B_LFO` behavior on LINE/LINE SEQ |
| save / defaults | `PARA` 1 s / 3 s chords |
| MIDI clock/notes | literal `midi_poll()` fed by `midi.c` |
| shed / overload | `app_overload()` from measured render time → `ps_shed(1)` |

---

## 7. Memory and CPU

Literal memory users:

- `echo_buf[2][20480] int16` = 81,920 B (`PLAT_POOL`);
- `cap_buf[2][45056] int16` = 180,224 B (`PLAT_POOL`);
- `gfx.c` canvas `240×144 uint16` = 69,120 B (`.pool`);
- pool total ≈ **331,264 B**, leaving ~12.8 KiB of the 344,064 B pool.

Non-pool explicit DSP buffers also need normal SRAM:

- SMEAR `ap_buf` 9,332 B, flanger 2,048 B, WASH 27,128 B + 6,456 B ≈ **43 KiB**;
- engine/events/line/AUTO state ≈ 6–8 KiB;
- `FACE[]` is 57,600 B of const flash data, not RAM.

The linker script asserts `.pool` and normal `.bss` do not overflow. If the canvas will not fit, define `APP_CANVAS_PX=240u*120u` at compile time (a build flag, not a source edit).

CPU: one 256-frame `app_render()` every 5.8 ms; the 64-frame ISR only copies samples. Compile host and device with `-ffp-contract=off` so `fastmath.h` matches. If a render exceeds 90 % of its block, call `app_overload()` so literal shedding reduces FLANGE/events/atmos load.

---

## 8. Build and tests

### 8.1 Build with the local toolchain

The JieLi toolchain and AC79 SDK are available:

```sh
export JIELI_TOOLCHAIN="$HOME/.jieli/toolchain"      # -> jieli-linux-toolchains-20260730.1
export AC79_SDK="$HOME/fw-AC79_AIoT_SDK"
# clang: "$JIELI_TOOLCHAIN/common/bin/clang"
```

`tools/build_pa.py`:

- compiles the unity file (`gfx.c`, `announce.c`, `touch.c`, `app.c`) at `-Os`;
- compiles `psy.c` separately at `-O2`;
- compiles `plat/pocketamp/*.c`;
- flags: `-O2/-Os -Wall -Wextra -ffreestanding -fno-builtin -ffp-contract=off`;
- links with `ld/pocketamp.ld`;
- packages with a new Pocket AMP packer derived from the AC79 SDK format.

Device builds are therefore part of the plan, not blocked.

### 8.2 Host tests

Keep RhinoTiger’s original host simulator (`base/host/plat_host.c`) as the engine regression suite. Add `plat/pa_host/plat_pa_host.c` that runs the **same literal `app.c`** with simulated Pocket AMP input:

| Test | Checks |
|---|---|
| `test_literal_diff.sh` | app/DSP/base files are byte-identical to upstream |
| `test_virtual_panel.c` | every gesture in §4 produces the expected virtual button/encoder/key |
| `test_audio_ring.c` | 64-frame halves produce continuous 256-frame `app_render()` blocks |
| `test_store_map.c` | objects stay inside `0x86000..0xB3FFF`; A/B validation works |
| `test_midi.c` | USB bytes become RhinoTiger packets with correct timestamps |
| existing `app_expect()` / `app_host_cmd()` scripts | pages, worlds, parameters, FX, saves unchanged |

### 8.3 Device tests

1. Cross-build and flash the new image.
2. Verify LCD logo, all 15 pages and both knobs.
3. Render a known world and check for glitches/NaN with device CPU telemetry.
4. Soak save/load cycles, including brownout/OTA flush.
5. Check USB MIDI clock/note response on hardware.

---

## 9. Risks

| Risk | Mitigation |
|---|---|
| Pool nearly full | linker assertions; optional smaller canvas via compile flag |
| Float DSP cost on pi32v2 | benchmark with local toolchain; `app_overload()` → `ps_shed(1)` |
| No per-key LEDs | screen shows the same state; lights calls are no-ops |
| Virtual-panel complexity | focus and key cursor shown on screen; chords give direct page access |
| MASTER reused as second knob | absolute value always drives gain; delta only emitted in selected focuses |
| Flash safety | new store maps away from forbidden addresses; deferred writes |
| USB/BLE stack written from scratch | BLE off initially; USB MIDI only |
| Licensing | preserve RhinoTiger notices; no Andromeda code carried over |

---

## 10. Licensing

- RhinoTiger app/DSP are **GPL-3.0-only**; the combined firmware is GPL-3.0-only.
- RhinoTiger `base/` files and `fastmath.h` are **MPL-2.0** and keep their file-level notices.
- Generated fonts may carry **OFL** notices from `base/assets/fonts`; preserve them.
- The Pocket AMP BSP headers in RhinoTiger’s `base/bsp/boards/pocketamp/` are MPL-2.0; keep their notices.
- No Andromeda source is included, so no Andromeda GPL/MPL/Apache notices are inherited.
- AC79 SDK material, if linked, follows its own license; keep it outside the app source tree.

---

## 11. Implementation sequence

1. Create the project layout, copy literal files, add the diff check.
2. Write `unity.c`, `lcd.c` and `gfx.c` bridge; verify the RhinoTiger logo and pages on device.
3. Write `input.c` virtual-panel translator; verify every page and parameter is reachable.
4. Write `audio.c` ring bridge; render a known world to a host WAV first, then device.
5. Write `store.c`, `midi.c` and remaining `plat_*` stubs.
6. Write `main_pa.c`, `ld/pocketamp.ld`, `tools/build_pa.py`.
7. Run host tests, then cross-build/flash/device soak tests.
8. Produce the final physical control card.

---

## 12. Acceptance criteria

- `app/`, `app/dsp/` and the used `base/` files are byte-for-byte identical to RhinoTiger.
- No Andromeda source file exists in the project tree.
- All 15 pages, 55 parameters, 8 worlds, 16 FX, 27 notes, sections, REC, AUTO PLAY, saves and MIDI work through the virtual panel.
- Two knobs and four musical keys suffice for normal play; `HOME`/`PARA` provide only modifier/save functions.
- Pool/link assertions pass; audio is glitch-free; save/load never touches forbidden flash.
- The project cross-builds with `$JIELI_TOOLCHAIN` and `$AC79_SDK` and passes both host and device tests.
