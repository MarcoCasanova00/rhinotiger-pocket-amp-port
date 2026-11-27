#!/usr/bin/env bash
# Pocket AMP device image: pi32v2 compile/link/pack (see build_pa.py).
set -euo pipefail
cd "$(dirname "$0")/.."
exec python3 tools/build_pa.py "$@"
