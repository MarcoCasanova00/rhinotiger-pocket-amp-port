#!/bin/sh
# Verify that every copied RhinoTiger source is byte-for-byte identical to upstream.
# The port is allowed to change only files under plat/, ld/, tools/ and docs/.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
UP=${RHINO_SRC:-/tmp/opencode/rhinotiger}
fail=0

check() {
    dst=$1
    src=$2
    if [ ! -f "$dst" ]; then
        echo "MISSING: $dst"
        fail=1
    elif [ ! -f "$src" ]; then
        echo "NO UPSTREAM: $src"
        fail=1
    elif ! cmp -s "$dst" "$src"; then
        echo "CHANGED: $dst"
        fail=1
    fi
}

for f in "$ROOT"/app/* "$ROOT"/app/.gitignore; do
    [ -f "$f" ] && check "$f" "$UP/app/$(basename "$f")"
done

for f in "$ROOT"/app/dsp/*; do
    [ -f "$f" ] && check "$f" "$UP/app/dsp/$(basename "$f")"
done

for f in plat.h fm1b_config.h fastmath.h; do
    check "$ROOT/base/include/$f" "$UP/base/include/$f"
done

check "$ROOT/base/device/gfx.c" "$UP/base/device/gfx.c"
check "$ROOT/base/device/announce.c" "$UP/base/device/announce.c"

for f in touch.c touch.h sense.c; do
    check "$ROOT/base/lib/$f" "$UP/base/lib/$f"
done

for f in "$ROOT"/base/bsp/boards/pocketamp/*.h; do
    [ -f "$f" ] && check "$f" "$UP/base/bsp/boards/pocketamp/$(basename "$f")"
done

for f in "$ROOT"/base/bsp/chip/*; do
    [ -f "$f" ] && check "$f" "$UP/base/bsp/chip/$(basename "$f")"
done

for f in gen_font.py gen_words.py; do
    check "$ROOT/base/tools/$f" "$UP/base/tools/$f"
done

for f in "$ROOT"/base/assets/fonts/*; do
    [ -f "$f" ] && check "$f" "$UP/base/assets/fonts/$(basename "$f")"
done

for f in "$ROOT"/tests/*; do
    [ -f "$f" ] && check "$f" "$UP/app/tests/$(basename "$f")"
done

if [ "$fail" -ne 0 ]; then
    echo "literal source check FAILED"
    exit 1
fi

echo "literal source check OK"
