# Pocket AMP control card

The literal RhinoTiger app's panel is 14 buttons, 7 encoder roles and 27
keys. The Pocket AMP has PARA (encoder + push), MASTER (pot) and four keys
( VOL−, VOL+, <, > ) plus HOME. This page is what USB port maps to what
the app sees.

## The two knobs

`PARA` click cycles a focus; `PARA` turns are the encoder of that focus.
`MASTER` is always the master gain; on WORLD and PARAM focuses its movement is
also the second knob.

| Focus | PARA turn emits | MASTER delta emits |
|---|---|---|
| PAGE | `EN_SELECT` (pages) | — |
| WORLD | `EN_PRESET` (worlds) | `EN_PRESET` |
| K1..K4 | `EN_K1..EN_K4` | same knob |
| ALGO | `EN_ALGO` | — |
| KEY | moves the key cursor | — |

## The four musical keys

Single keys, and the chords from the 15 ms-debounced stable mask (component
keys are not emitted separately):

| Gesture | Virtual button | What the literal app does |
|---|---|---|
| `VOL−` tap/hold | `B_PLAY` | play/rest; tap at the logo starts it |
| `VOL+` tap | `B_ARP` | build |
| `VOL+` hold 500 ms | `B_ARP` held | drop |
| `<` tap | `B_SAVE` | break cue; save on GLOBAL |
| `<` hold 1500 ms on GLOBAL | `B_SAVE` held | restore defaults |
| `>` tap | `B_REC` | arm/take |
| `>` hold 500 ms | `B_REC` held | clear |
| `VOL−`+`VOL+` | `B_HOME` | MAIN / GROOVE |
| `<`+`>` | `B_SEQ` | BREAK / BUILD / AUTO |
| `VOL−`+`<` | `B_SEL` | KICK / BASS |
| `VOL+`+`>` | `B_ENV` | PERC / EVENTS |
| `VOL−`+`>` | `B_LFO` | LINE / LINE SEQ / ATMOS |
| `VOL+`+`<` | `B_EDIT` | MIX |
| `HOME`+`VOL−` | `B_GLO` | GLOBAL |
| `HOME`+`VOL+` | `B_FX` | ECHO |
| `HOME`+`<` / `HOME`+`>` | `B_OCTDN` / `B_OCTUP` | throw-key octave |
| `PARA` hold 1 s, release | `B_GLO` then `B_SAVE` tap | save settings |
| `PARA` hold 3 s | `B_GLO`+`B_SAVE` held 1.6 s | factory defaults |

## The KEY focus

`PARA` clicks to KEY. The four keys then play the key cursor: `VOL−` and
`VOL+` both hold the cursor's key (events throw, the FX grid when on ECHO),
`<`/`>` (and PARA turns) step the cursor 0..26. `HOME` combos still work.

## Notes

- `MASTER` is read continuously as `plat_master()` (0..4096) and passed to
  `app_render()` as the render gain.
- The Pocket AMP has no per-key LEDs; the literal screen shows the same state.
- No knob acceleration is added: the app's own knob behavior stands.
