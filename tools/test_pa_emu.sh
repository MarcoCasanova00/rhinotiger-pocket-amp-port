#!/usr/bin/env bash
# Pocket AMP control-translation regression: tools/emu_pa.py --serial against
# the literal app on the host simulator, through the firmware's own pa_virt.c.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT=$("$ROOT/tools/emu_pa.py" --serial < "$ROOT/tests/pa_emu.txt" 2>&1 || true)
fails=$(printf '%s\n' "$OUT" | grep -c '^FAIL' || true)
printf '%s\n' "$OUT" | grep -E '^(FAIL|  ok|\(no)' | sed 's/^/  /' || true
if [ "$fails" -gt 0 ]; then
    echo "pa emu: $fails FAILURES"
    exit 1
fi
echo "pa emu: all passed"
