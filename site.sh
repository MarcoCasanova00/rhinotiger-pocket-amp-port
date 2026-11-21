#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 hacha
# RhinoTiger's site: fm1-base's (tools/make_site.py: the front page, the installer, the manual, the emulator), its
# pages in English and Japanese (app/site/pages.py), in RhinoTiger's colours (app/site/theme.py), into app/build/site;
# with --push, to the gh-pages branch (replacing it).
#   ./site.sh --preview                  only the front page and the manuals (no package, no Emscripten)
#   ./site.sh VERSION PACKAGE [--push]
# PACKAGE: the release's attested package (gh release download vX.Y -p '*.fwsc'). Needs Emscripten (the emulator).
set -e
cd "$(dirname "$0")"
PY="${PYTHON:-python3}"
OUT=app/build/site
if [ "${1:-}" = "--preview" ]; then
  rm -rf "$OUT"
  "$PY" app/site/pages.py "$OUT"
  "$PY" app/site/theme.py "$OUT" >/dev/null
  echo "preview in $OUT: open $PWD/$OUT/manual/index.html (or manual/ja/index.html)"
  exit 0
fi
V="${1:?usage: ./site.sh --preview | VERSION PACKAGE [--push]}"
PKG="${2:?usage: ./site.sh --preview | VERSION PACKAGE [--push]}"
[ -f "$PKG" ] || { echo "site.sh: no package $PKG" >&2; exit 1; }
"$PY" base/tools/build.py app --web --release "$V"
"$PY" base/tools/make_site.py app "$PKG" "$V" "$OUT"
"$PY" app/site/pages.py "$OUT" "$V" "$(ls "$OUT"/firmware/rhinotiger-*.fwsc | head -1 | xargs basename)"
"$PY" app/site/theme.py "$OUT"
[ "${3:-}" = "--push" ] || { echo "site in $OUT (./site.sh $V $PKG --push to publish it)"; exit 0; }
REMOTE="$(git remote get-url origin)"
SHA="$(git rev-parse --short HEAD)"
cd "$OUT"
rm -rf .git
git init -q -b gh-pages
git add -A
git commit -q -m "Site for rhinotiger $V (from $SHA)"
git push -q -f "$REMOTE" gh-pages
rm -rf .git
echo "pushed the site for rhinotiger $V to gh-pages"
