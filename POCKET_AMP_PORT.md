# POCKET AMP PORT

This is a fork of [hacha/rhinotiger](https://github.com/hacha/rhinotiger)
labeled as the **POCKET AMP PORT**: the literal RhinoTiger app and DSP,
ported to the **M-VAVE / Donner Pocket AMP** (two knobs, four keys, HOME,
240×240 screen).

The app and DSP sources (`app/`, `app/dsp/`) are the upstream files byte for
byte; the platform layer is new code under `plat/pocketamp/`, the device build
is `tools/build_pa.py` + `ld/pocketamp.ld`, and the host emulator/browser
demo/flasher live in `tools/` (`emu_pa_server.py`, `pa_pkg.sh`,
`pa_flash_check.sh`, …).

## Layout

- `app/`, `app/dsp/`, `tests/` — literal upstream (GPL-3.0-only)
- `base/` — the upstream base subset used by the app (MPL-2.0 and other
  notices preserved; includes the Pocket AMP board headers under
  `base/bsp/boards/pocketamp/`)
- `plat/pocketamp/` — the port's platform: input translation (`pa_virt.c`),
  ALNK0 audio bridge, A/B store, `plat_*` contract, boot
- `ld/pocketamp.ld` — the Pocket AMP standalone linker map
- `tools/` — device build, literal checks, host regression, terminal +
  browser emulators, web-installable package (`rhinotiger-pa.fwsc`)
- `docs/` — design, control card, provenance, flashing

## Status

Software-complete and verified as far as possible without the unit: the
device image builds with the JieLi pi32v2 toolchain, the literal sources pass
the check, the app's host regression passes, the control translation is
regression-tested, and the package is accepted by Pendel's installer. The
remaining step is flashing a unit and the bench soak (see `docs/FLASHING.md`).

## Credits

- **RhinoTiger** — app and DSP by hacha (GPL-3.0-only), upstream commit
  `7647586` (see `docs/SOURCE_PROVENANCE.md`)
- **fm1-base** — host/device platform pieces by Charles Vestal (MPL-2.0),
  upstream commit `968d570`
- **BSP / Pocket AMP board headers** — Hügelton Instruments BSP family by Leo
  Kuroshita (MPL-2.0), BSP commit `18eadab`; the `pa_*` headers are the ones
  used by Pendel
- **Pendel** — open-source Pocket AMP firmware by Leo Kuroshita (GPL-3.0);
  its packaging/installing tools produce and install our `rhinotiger-pa.fwsc`
  (run externally; none of its code is copied into this fork)
- **Pocket AMP hardware** — M-VAVE / Donner
- **JieLi AC79 SDK** — three files are embedded in the `.fwsc` package
  (Apache-2.0, listed in `LICENSING.md`)
- Generated fonts keep their own licences (OFL)

No Andromeda (the Pocket AMP's stock firmware) source is included.

## Licensing

GPL-3.0-only for the app/DSP and this port's new code as a whole; the base
components keep their own notices (MPL-2.0, Apache-2.0 where noted). No
Andromeda (the Pocket AMP's stock firmware) source is included.
