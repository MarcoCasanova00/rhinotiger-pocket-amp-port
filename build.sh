#!/bin/sh
# SPDX-License-Identifier: MPL-2.0
# Copyright (C) 2026 Charles Vestal
# Changes Copyright (C) 2026 hacha
# Build RhinoTiger with fm1-base (base/, a git submodule). AMB-1's build.sh (charlesvestal/fm1-amb, MPL-2.0), its
# name changed.
#   ./build.sh [--release X.Y] [--host | --web]
#   JIELI_TOOLCHAIN  JieLi Linux toolchain (default: ~/.jieli/toolchain)
#   AC79_SDK         JieLi AC79 SDK checkout (default: ~/fw-AC79_AIoT_SDK)
#   PYTHON           a Python with Pillow
#   EMSDK            Emscripten SDK, read when emcc is not on PATH (default: ~/emsdk)
set -e
cd "$(dirname "$0")"
export JIELI_TOOLCHAIN="${JIELI_TOOLCHAIN:-$HOME/.jieli/toolchain}"
export AC79_SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"
EMSDK="${EMSDK:-$HOME/emsdk}"
if ! command -v emcc >/dev/null 2>&1 && [ -f "$EMSDK/emsdk_env.sh" ]; then
    HERE="$(pwd)"
    cd "$EMSDK" && . ./emsdk_env.sh >/dev/null 2>&1 || true
    cd "$HERE"
fi
PY="${PYTHON:-python3}"
[ -f base/tools/build.py ] || { echo "build.sh: no fm1-base in base/ (git submodule update --init --recursive)"; exit 1; }
"$PY" -c 'import PIL' 2>/dev/null || { echo "build.sh: $PY has no Pillow (pip3 install Pillow)"; exit 1; }
exec "$PY" base/tools/build.py app "$@"
