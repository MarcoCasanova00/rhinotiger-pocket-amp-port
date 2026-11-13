# RhinoTiger（犀虎）

A generative psytrance firmware for the M-VAVE FM-1, from dark psy to hi-tech. It plays by itself; you steer it:
macros and mutes as in DUB-1, NACHTFAHRT and HYP-1, and keys that throw events into the music.

Status: just started from fm1-base's template.

## Build

```sh
git submodule update --init --recursive
./build.sh --host                     # host simulator (a C compiler is enough)
./test.sh                             # the host simulator, every script in app/tests
./build.sh --web                      # browser emulator (Emscripten)
./build.sh                            # device package (JieLi toolchain + AC79 SDK)
```

## Licence

GPL-3.0-only (see LICENSE). Built on [fm1-base](https://github.com/charlesvestal/fm1-base) (MPL-2.0) and
felucca-bsp. Files taken from fm1-base's template keep their MPL-2.0 notices.
