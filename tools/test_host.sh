#!/bin/sh
# Run RhinoTiger's original host tests against the copied literal app.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT="$ROOT/app/build/host"
EXE="$OUT/rhinotiger_host"
[ -x "$EXE" ] || "$ROOT/tools/build_host.sh" >/dev/null

fails=0
if [ "$#" -gt 0 ]; then
    set -- "$@"
else
    set -- "$ROOT"/tests/*.txt
fi
for t in "$@"; do
    name=$(basename "$t" .txt)
    rc=0
    log=$("$EXE" "$t" "$OUT" 2>&1) || rc=$?
    oks=$(printf '%s\n' "$log" | grep -c '^  ok' || true)
    case $rc in
        0) echo "ok    $name ($oks checks)" ;;
        1) echo "FAIL  $name"; printf '%s\n' "$log" | grep FAIL || true; fails=$((fails + 1)) ;;
        *) echo "ERROR $name (exit $rc)"; printf '%s\n' "$log" | tail -3; fails=$((fails + 1)) ;;
    esac
done
[ "$fails" -eq 0 ] || { echo "$fails failed"; exit 1; }
echo "all passed"
