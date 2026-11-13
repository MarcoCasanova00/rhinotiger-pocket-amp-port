#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 hacha
# Build RhinoTiger for the FM-1 and install it over USB-MIDI (fm1-base's tools/fm1_install.py).
#   ./install.sh [--no-build] [fm1_install.py's options: --port NAME, --yes, --classic, ...]
#   ./install.sh --info              the connected FM-1's identity only
#   PYTHON  a Python with Pillow (the build) and the MIDI module fm1_install.py uses
set -e
cd "$(dirname "$0")"
PY="${PYTHON:-python3}"
PKG=app/build/rhinotiger.fwsc
build=1
case "$1" in
--no-build) build=0; shift ;;
--info) exec "$PY" base/tools/fm1_install.py "$@" ;;
esac
if [ $build -eq 1 ]; then
    ./build.sh
fi
[ -f "$PKG" ] || { echo "install.sh: no $PKG (./build.sh first)"; exit 1; }
exec "$PY" base/tools/fm1_install.py "$PKG" "$@"
