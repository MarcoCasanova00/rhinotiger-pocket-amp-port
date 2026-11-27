#!/usr/bin/env bash
# Build the Pocket AMP port as a static browser emulator (WebAssembly).
# Needs Emscripten on PATH (e.g. source $HOME/emsdk/emsdk_env.sh).
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT="$ROOT/build/web"
mkdir -p "$OUT"
if ! command -v emcc >/dev/null; then
    for e in /home/maltomare/emsdk/upstream/emscripten/emcc "$HOME/emsdk/upstream/emscripten/emcc"; do
        [ -x "$e" ] && { export PATH="$(dirname "$e"):$PATH"; break; }
    done
fi
command -v emcc >/dev/null || { echo "emcc not found (source emsdk_env.sh)" >&2; exit 1; }

# the host simulator (base/host/plat_host.c): in the fork's base, or upstream
HOST_DIR=${PA_HOST_DIR:-}
[ -z "$HOST_DIR" ] && [ -d "$ROOT/base/host" ] && HOST_DIR="$ROOT/base/host"
[ -z "$HOST_DIR" ] && HOST_DIR=/tmp/opencode/rhinotiger/base/host
[ -f "$HOST_DIR/plat_host.c" ] || { echo "no plat_host.c (set PA_HOST_DIR)" >&2; exit 1; }
# plat_host.c is the host unity: pull its siblings from the same base
UP_BASE=$(dirname "$HOST_DIR")

emcc -O2 -ffp-contract=off -std=gnu99 -Wall -Wno-unused-function \
     -Wno-unused-parameter -Wno-unused-variable -DPLAT_HOST -DPLAT_WEB \
     -I"$HOST_DIR" -I"$UP_BASE/device" -I"$UP_BASE/include" -I"$UP_BASE/lib" \
     -I"$ROOT/app" -I"$ROOT/app/dsp" -I"$ROOT/app/build/gen" \
     -I"$ROOT/plat/pocketamp" \
     --no-entry -sSTANDALONE_WASM -sSTACK_SIZE=1048576 \
     -sINITIAL_MEMORY=33554432 -sFILESYSTEM=0 \
     -sEXPORTED_FUNCTIONS=_web_boot,_web_pa,_web_render,_web_out_l,_web_out_r,_web_fb,_web_blits,_web_now,_web_lit_buttons,_web_lit_keys,_web_dim_buttons,_web_dim_keys \
     -o "$OUT/app.wasm" "$ROOT/web/emu_pa_web.c" "$ROOT/app/dsp/psy.c"

echo "build/web/app.wasm: $(stat -c%s "$OUT/app.wasm") B"
