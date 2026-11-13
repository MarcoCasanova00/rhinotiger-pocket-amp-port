#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 hacha
# The click sweep: every part alone in every world (tools/sweep-stems.txt) and the effects switched over the music
# (tools/sweep-fx.txt), recorded in the host simulator and searched for lone discontinuities (tools/clicks.py).
# Each file should show 0. Usage: tools/sweep.sh
set -e
cd "$(dirname "$0")/.."
./build.sh --host >/dev/null
OUT=app/build/host
for s in stems fx; do
    "$OUT/rhinotiger_host" "tools/sweep-$s.txt" "$OUT" >/dev/null
done
python3 tools/clicks.py $OUT/clk-*.wav
