#!/usr/bin/env bash
# Build a web-installable .fwsc package of the RhinoTiger port for the Pocket
# AMP, using Pendel's packer (GPL-3.0, run externally; nothing copied here).
#
# The package rides the stock firmware's two-step updater (step 1: the
# running firmware checks the package and starts the package's loader; step 2:
# the loader writes the app area only). It can then be installed:
#   - from a browser (WebMIDI, Chrome/Edge) with Pendel's web installer, or
#   - from the terminal: python3 ~/Pendel/tools/pa_install.py PACKAGE.fwsc --yes
#
# Usage:
#   tools/pa_pkg.sh                 # builds build/pa/pkg/rhinotiger-pa.fwsc
#   PENDEL_LOADER=/path/loader.bin tools/pa_pkg.sh   # use an existing loader image
#   PENDEL_TOOLS=$HOME/Pendel tools/pa_pkg.sh --product "Pocket AMP_910"
set -euo pipefail
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
PENDEL=${PENDEL_TOOLS:-$HOME/Pendel}
SDK=${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}
APP="$ROOT/build/pa/app.bin"
PKGDIR="$ROOT/build/pa/pkg"
PRODUCT="Pocket AMP_900"

while [ "$#" -gt 0 ]; do
    case "$1" in
        --product) PRODUCT="$2"; shift 2 ;;
        *) echo "unknown: $1" >&2; exit 2 ;;
    esac
done

[ -f "$APP" ] || { echo "build tools/build_pa.py first (no $APP)" >&2; exit 1; }
[ -d "$SDK" ] || { echo "AC79_SDK not found at $SDK" >&2; exit 1; }
if [ ! -d "$PENDEL" ]; then
    echo "cloning Pendel (tools only) into $PENDEL ..."
    git clone --depth 1 https://github.com/hugelton/Pendel "$PENDEL"
fi
[ -f "$PENDEL/tools/pa_pkg_make.py" ] || { echo "missing $PENDEL/tools/pa_pkg_make.py" >&2; exit 1; }

mkdir -p "$PKGDIR"

# the update loader's raw RAM image: given, built, or fetched from Pendel's release
LOADER=${PENDEL_LOADER:-}
if [ -z "$LOADER" ] && [ -f "$PENDEL/build/pa/loader/loader.bin" ]; then
    LOADER="$PENDEL/build/pa/loader/loader.bin"
fi
if [ -z "$LOADER" ]; then
    echo "fetching Pendel's update loader from its latest release ..."
    python3 "$ROOT/tools/pa_fetch_loader.py" \
        "$PKGDIR/loader-pendel.bin" --pendel "$PENDEL" \
        --keep-package "$PKGDIR/pendel-release.fwsc"
    LOADER="$PKGDIR/loader-pendel.bin"
fi

echo "packaging $APP + $LOADER -> $PKGDIR/rhinotiger-pa.fwsc ($PRODUCT)"
AC79_SDK="$SDK" python3 "$PENDEL/tools/pa_pkg_make.py" \
    "$APP" "$LOADER" "$PKGDIR/rhinotiger-pa.fwsc" \
    --product "$PRODUCT" --sdk "$SDK"

echo
echo "installed:"
echo "  browser : Pendel's web installer (Chrome/Edge, WebMIDI), plug in the Pocket AMP,"
echo "            choose $PKGDIR/rhinotiger-pa.fwsc and press Install"
echo "  terminal: python3 $PENDEL/tools/pa_install.py $PKGDIR/rhinotiger-pa.fwsc --yes"
