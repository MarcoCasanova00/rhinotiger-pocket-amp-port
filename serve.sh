#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 hacha
# Serve the browser emulator (app/build/emu, made by ./build.sh --web) on http://localhost:PORT/ (default 8000).
# Every reply says Cache-Control: no-store, so a reload after a rebuild gets the new app.wasm, not the browser's copy.
# Usage: ./serve.sh [port]
set -e
cd "$(dirname "$0")"
PORT="${1:-8000}"
DIR=app/build/emu
if [ ! -f "$DIR/index.html" ]; then
    echo "serve: no $DIR/index.html; build it first: ./build.sh --web" >&2
    exit 1
fi
echo "serving $DIR on http://localhost:$PORT/ (Ctrl-C to stop)"
exec python3 -c '
import functools, http.server, sys
class H(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cache-Control", "no-store")
        super().end_headers()
h = functools.partial(H, directory=sys.argv[2])
try:
    http.server.ThreadingHTTPServer(("127.0.0.1", int(sys.argv[1])), h).serve_forever()
except KeyboardInterrupt:
    pass
' "$PORT" "$DIR"
