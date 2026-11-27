#!/bin/sh
# Build the host simulator for the copied literal RhinoTiger app.
# Uses fm1-base's canonical build.py from the upstream checkout; no port code is involved.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
BASE=${RHINO_BASE:-/tmp/opencode/rhinotiger/base}
PY=${PYTHON:-python3}
[ -f "$BASE/tools/build.py" ] || { echo "no fm1-base build.py at $BASE" >&2; exit 1; }
"$ROOT/tools/check_literal.sh"
"$PY" "$BASE/tools/build.py" "$ROOT/app" --host
