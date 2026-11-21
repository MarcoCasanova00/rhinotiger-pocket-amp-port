# RhinoTiger

![The logo: a face, half rhino and half tiger, with 犀 over 虎 in rainbow colours and RHINOTIGER across the middle](manual/img/screen-logo.png "RhinoTiger")

RhinoTiger is a generative psytrance firmware for the M-VAVE FM-1, from dark psy to hi-tech. It plays by itself, and
you steer it: four macro knobs, a mixer with mutes, and keys that throw one-shot sounds into the music. A rolling FM
bass, percussion, a repeating line, zaps and gurgles, and an atmosphere under it all; breaks, builds and drops that
come on their own or when you call them; sixteen effects to play on the keys; and eight worlds, from slow and wet to
fast and fine.

![RhinoTiger playing: COLD SIGNAL at 152, the five lanes of the bar and the four macros](manual/img/screen-playing.png "RhinoTiger on the FM-1's screen")

This is version {{VERSION}}. You install it at your own risk, and the installer can put M-VAVE's own firmware back.

- [Try it in the browser](emu/). Play it right here, with sound. It runs the same code as the FM-1, so you don't
  need one.
- [Install](install/) it from Chrome or Edge, with the FM-1 connected by USB.
- The [Manual](manual/) explains the screen, the pages, the keys and the effects, the sections and the worlds.
- [Download {{PKG}}](firmware/{{PKG}}) to use the command-line installer: `python3 tools/fm1_install.py {{PKG}}`.
- The [Source](https://github.com/hacha/rhinotiger) is under GPL-3.0-only. It's built on fm1-base (MPL-2.0) and
  felucca-bsp, which come from Felucca by Leo Kuroshita, Hügelton Instruments, and it was started from fm1-base's
  template by Charles Vestal. The logo's characters were drawn from Noto Sans JP Black (SIL Open Font License 1.1).

M-VAVE and FM-1 are trademarks of their owners. RhinoTiger is not affiliated with M-VAVE.
