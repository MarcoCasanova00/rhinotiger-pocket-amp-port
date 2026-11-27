#!/usr/bin/env bash
# Readiness check for flashing the RhinoTiger port to a Pocket AMP
# (docs/FLASHING.md). Prints what is present and what is missing.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
PENDEL=${PENDEL_TOOLS:-$HOME/Pendel}
ok=0; miss=0
say(){ if [ "$2" = 0 ]; then echo "  ok    $1"; else echo "  MISS  $1"; fi; }

echo "RhinoTiger Pocket AMP — flash readiness"
[ -f "$ROOT/build/pa/app.bin" ] && { say "app.bin ($(stat -c%s "$ROOT/build/pa/app.bin") B)" 0; ok=$((ok+1)); } \
  || { say "build/pa/app.bin (run tools/build_pa.py)" 1; miss=$((miss+1)); }
[ -f "$ROOT/build/pa/pkg/rhinotiger-pa.fwsc" ] && { say "rhinotiger-pa.fwsc package ($(stat -c%s "$ROOT/build/pa/pkg/rhinotiger-pa.fwsc") B)" 0; ok=$((ok+1)); } \
  || { say "build/pa/pkg/rhinotiger-pa.fwsc (run tools/pa_pkg.sh)" 1; miss=$((miss+1)); }
command -v python3 >/dev/null && { say "python3" 0; ok=$((ok+1)); } || { say "python3" 1; miss=$((miss+1)); }
python3 -c 'import mido, rtmidi' 2>/dev/null && { say "mido + python-rtmidi" 0; ok=$((ok+1)); } \
  || { say "mido + python-rtmidi (pip install mido python-rtmidi)" 1; miss=$((miss+1)); }
if [ -d "$PENDEL" ]; then
  say "Pendel tools at $PENDEL" 0; ok=$((ok+1))
  for t in pa_dev.py pa_uboot_dump.py pa_make_image.py pa_uboot_write.py; do
    [ -f "$PENDEL/tools/$t" ] && { say "Pendel tools/$t" 0; ok=$((ok+1)); } \
      || { say "Pendel tools/$t missing" 1; miss=$((miss+1)); }
  done
else
  say "Pendel tools (git clone https://github.com/hugelton/Pendel ~/Pendel)" 1
  miss=$((miss+1))
fi
command -v sudo >/dev/null && { say "sudo (for jl-uboot-tool steps)" 0; ok=$((ok+1)); } \
  || { say "sudo" 1; miss=$((miss+1)); }
[ -n "${JLTOOL:-}" ] && { say "JLTOOL=$JLTOOL" 0; ok=$((ok+1)); } \
  || { say "JLTOOL env (path to jl-uboot-tool; needed for dump/write)" 1; miss=$((miss+1)); }

echo "ok=$ok missing=$miss"
[ "$miss" -eq 0 ] || exit 1
