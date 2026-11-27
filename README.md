# RhinoTiger（犀虎） — POCKET AMP PORT

![RhinoTiger](app/docs/img/screen-logo.png)

[![License](https://img.shields.io/github/license/MarcoCasanova00/rhinotiger)](LICENSE)
[![Web emulator](https://img.shields.io/badge/-Web%20emulator-important?style=for-the-badge&logo=googlechrome&logoColor=white)](https://marcocasanova00.github.io/rhinotiger/emulator/)
[![Web installer](https://img.shields.io/badge/-Web%20installer-success?style=for-the-badge)](https://marcocasanova00.github.io/rhinotiger/installer/)

> **POCKET AMP PORT** — this fork adds a port of RhinoTiger to the
> M-VAVE / Donner Pocket AMP. The app and DSP stay literal upstream;
> the port lives under `plat/pocketamp/`, `ld/`, `tools/` and `docs/`
> (see `POCKET_AMP_PORT.md`). The upstream README below describes the
> original FM-1 firmware.
>
> **Web emulator** — runs in the browser (WASM of the literal app + the
> Pocket AMP translator): https://marcocasanova00.github.io/rhinotiger/emulator/
> Build it with `tools/build_web_pa.sh` (needs Emscripten). The browser
> flasher for the real Pocket AMP (`rhinotiger-pa.fwsc`) is on the same site
> and in `docs/FLASHING.md`.

A generative psytrance firmware for the M-VAVE FM-1, from dark psy to hi-tech. It plays by itself; you steer it.

A kick, a rolling FM bass, percussion, a repeating line, one-shot sounds and a held atmosphere play on a grid of
sixteenths, in eight worlds. You steer with four macro knobs and a mix with mutes; the keys throw one-shot sounds into the
music, or play sixteen effects over it; AUTO PLAY plays a whole set by itself. The manual:
[app/docs/MANUAL.md](app/docs/MANUAL.md) (日本語: [app/docs/MANUAL.ja.md](app/docs/MANUAL.ja.md)); the site, to try it in
the browser and install it: https://hacha.github.io/rhinotiger/

It reads nothing another firmware saved (projects, presets, samples), and that may not be there when you go back to
it: back it up first.

## Credits

This port is a fork of **RhinoTiger** by [hacha](https://github.com/hacha)
(app and DSP, GPL-3.0-only) and of its base, **fm1-base**, by
[Charles Vestal](https://github.com/charlesvestal) (host/device platform
pieces, MPL-2.0). The chip and Pocket AMP board headers come from the
Hügelton Instruments BSP family by [Leo Kuroshita](https://github.com/kurogedelic)
(MPL-2.0), reused in the Pocket AMP firmware Pendel, whose tooling we also use
for packaging and installing (GPL-3.0-only, run externally; nothing of it is
copied here). The installer's loader is Pendel's (GPL-3.0). The Pocket AMP
hardware is M-VAVE / Donner; the package embeds three JieLi AC79 SDK files
(Apache-2.0, see `LICENSING.md`). Generated fonts keep their own licences
(OFL). **No Andromeda (the Pocket AMP's stock firmware) source is included.**
See `docs/SOURCE_PROVENANCE.md` for the exact upstream commits.


## Controls

| Control | Does |
|---|---|
| SELECT | turns through the 15 pages, MAIN to GLOBAL |
| HOME | MAIN: the four macros Energy, Detail, Dark, Drift; again: GROOVE (Tempo, Root, Fills, Sit out) |
| SEQ | BREAK (Breaks, Length, Gap, Morph); again: BUILD (type, ending, length); again: AUTO (AUTO PLAY, Minutes, Seed, Perform) |
| SEL | KICK (Tune, Drive, Punch, Decay); again: BASS (Bite, Decay, Length, Drive) |
| ENV | PERC (Hats, Open, Clap, Tone); again: EVENTS (Density, Runs, Pitch, Spread) |
| LFO | LINE (Bite, Decay, Length, Drive); again: LINE SEQ (Density, Mutate, Range, Slide); again: ATMOS (Colour, Air, Motion, Depth). Held on LINE or LINE SEQ: the phrase is drawn anew |
| EDIT | MIX: a part's Level, Send and Mute |
| FX | ECHO (Feedback, Tone, Time, Level); the keys play the effects |
| GLO | GLOBAL (Lights, Theme, CPU); SAVE here keeps everything, held: the defaults |
| PRESETS | the world, from the next bar |
| ALGORITHM | the type of the page's part (on MAIN: the kit; on ECHO: the kind of echo) |
| K1..K4 | the four knobs of the page shown |
| PLAY | plays or rests |
| SAVE | on GLOBAL: keeps everything; elsewhere: calls a break |
| ARP | calls a build, then the drop |
| REC | takes what the keys throw into a loop of up to four bars, from the next bar; held: clears it |
| OCT- / OCT+ | the octave of the keys' sounds, from one down to two up |
| Keys | the 16 white keys throw one sound each; a black key held with one turns it into a rhythm (eighths, triplets, gallop, scatter, climb, fall...) |
| Keys on ECHO | the white keys are sixteen effects while held (repeats, filters, wash, dub, freeze, reverse, tape, riser, stairs, gate); the black keys mute the six parts, fill, half time, gap, kick and bass out, all back |

Every button glows dim; the one whose page shows is lit.

The eight worlds, picked with PRESETS:

- SWAMP RADIO (145): low and muffled, a soft stuttering bass, a loud atmosphere.
- UNDER THE ROOTS (148): deep and long, a thick low bass, a dark swell.
- COLD SIGNAL (152): cold dark psy, hard short sixteenths, a short line creeping in semitones. The one it starts in.
- RUBBER CIRCUS (156): bouncing, a talking line, gurgles, bars of triplets.
- BLACK UNDERTOW (160): heavy, a distorted bass, a long dragging line, big breaks.
- COPPER NERVE (168): tight, a short metallic repeat, staircases, a ride on the eighths.
- GLASS INSECTS (176): bright hi-tech, clean sixteenths, a bright metallic line, staircases flying left and right.
- CHROME SWARM (186): fast and fine, the densest world, almost no atmosphere in the groove, breaks of only four bars.

## Build

```sh
git submodule update --init --recursive
./build.sh --host                     # host simulator (a C compiler is enough)
./test.sh                             # the host simulator, every script in app/tests (or: ./test.sh basic)
./build.sh --web                      # browser emulator (Emscripten)
./serve.sh [port]                     # serve it on http://localhost:8000/
./site.sh X.Y PKG [--push]            # the site (app/build/site) from a release's package; --push: to gh-pages
./build.sh                            # device package (JieLi toolchain + AC79 SDK)
./install.sh                          # device package, then installed over USB-MIDI (--no-build: as built)
```

## Docs for maintainers

- [docs/autoplay-spec.ja.md](docs/autoplay-spec.ja.md): the AUTO PLAY specification (Japanese).
- [LICENSING.md](LICENSING.md): what is under which licence.

## Licence

GPL-3.0-only (see LICENSE). Built on [fm1-base](https://github.com/charlesvestal/fm1-base) (MPL-2.0) and
felucca-bsp. See LICENSING.md.
