#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Fetch Pendel's latest release package and extract its update loader
(ota.bin) as a raw RAM image, for wrapping our app image with Pendel's
pa_pkg_make.py.

The loader is hardware/firmware-neutral for the app area: Pendel ships the
same loader in every package, and the app area it writes is exactly ours
(0x4000..). Nothing of Pendel's firmware is used otherwise.

  pa_fetch_loader.py OUT_LOADER.bin [--pendel DIR] [--release TAG]
"""
import argparse
import json
import os
import struct
import sys
import urllib.request
from pathlib import Path


def enc(buf, off, size, key=0xFFFF):
    for i in range(size):
        buf[off + i] ^= key & 0xFF
        key = ((key << 1) ^ (0x1021 if key & 0x8000 else 0)) & 0xFFFF


def unpack_package(raw):
    """.fwsc -> logical image (remove the 20 marker bytes)"""
    if len(raw) < 20 * 0x30:
        raise SystemExit("not a package (too short)")
    logical = bytearray()
    for i in range(20):
        logical += raw[i * 0x30:(i + 1) * 0x30 - 1]
    logical += raw[20 * 0x30:]
    return bytes(logical)


def parse_ufw(logical):
    """logical image -> {name: bytes} (entries decrypted with the default key)"""
    if len(logical) < 0x40:
        raise SystemExit("logical image too short")
    hdr = bytearray(logical[:0x40])
    enc(hdr, 0, 0x40)                       # the packer encrypted header+list with key 0xFFFF
    total, count = struct.unpack_from("<IH", hdr, 4)
    files = {}
    for i in range(count):
        e = bytearray(logical[0x40 + i * 0x50:0x40 + (i + 1) * 0x50])
        enc(e, 0, 0x50)
        typ, _, crc, _, off, ln, _ = struct.unpack_from("<HHHHIII", e, 0)
        name = e[0x40:].split(b"\0", 1)[0].decode(errors="replace")
        files[name] = (typ, off, ln, crc)
    return files


def unwrap_ota(ota, lz4blk):
    """outer/inner JLFS heads + LZ4 blocks -> the loader's raw RAM image.

    The heads are packed without padding; rather than depend on their exact
    offsets we scan for the first block that decompresses to its declared
    length (clen/dlen), then decompress the whole stream."""
    n = len(ota)
    if n < 64:
        raise SystemExit("ota.bin: too short")
    for p in range(56, min(n, 96)):
        clen, dlen = struct.unpack_from("<II", ota, p)
        if not (0 < clen <= 8192 and 0 < dlen <= 8192):
            continue
        if p + 8 + clen > n:
            continue
        try:
            raw = lz4blk.decompress(ota[p + 8:p + 8 + clen])
        except Exception:
            continue
        if len(raw) != dlen:
            continue
        out = bytearray(raw)
        q = p + 8 + clen
        while q + 8 <= n:
            c2, d2 = struct.unpack_from("<II", ota, q)
            if not (0 < c2 <= 8192 and 0 < d2 <= 8192) or q + 8 + c2 > n:
                break
            try:
                r = lz4blk.decompress(ota[q + 8:q + 8 + c2])
            except Exception:
                break
            if len(r) != d2:
                break
            out += r
            q += 8 + c2
        return bytes(out), p - 6 if p >= 6 else 0, 0, b"ota"
    raise SystemExit("ota.bin: no LZ4 block stream found")


def latest_release(tag=None):
    url = ("https://api.github.com/repos/hugelton/Pendel/releases/" +
           (tag if tag else "latest"))
    with urllib.request.urlopen(url, timeout=30) as r:
        rel = json.load(r)
    assets = [a for a in rel.get("assets", []) if a["name"].endswith(".fwsc")]
    if not assets:
        raise SystemExit(f"release {rel.get('tag_name')}: no .fwsc asset")
    return rel["tag_name"], assets[0]["browser_download_url"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out", type=Path, help="the loader's raw RAM image (loader.bin)")
    ap.add_argument("--pendel", type=Path, default=Path.home() / "Pendel",
                    help="Pendel checkout (imports its lz4blk)")
    ap.add_argument("--release", help="a specific release tag (default: latest)")
    ap.add_argument("--keep-package", type=Path, help="also keep the downloaded .fwsc here")
    a = ap.parse_args()

    sys.path.insert(0, str(a.pendel / "tools"))
    try:
        import lz4blk  # noqa: F401
    except ImportError:
        raise SystemExit(f"no Pendel tools at {a.pendel} (git clone https://github.com/hugelton/Pendel)")

    tag, url = latest_release(a.release)
    print(f"downloading Pendel {tag}: {url}")
    with urllib.request.urlopen(url, timeout=120) as r:
        pkg = r.read()
    if a.keep_package:
        a.keep_package.parent.mkdir(parents=True, exist_ok=True)
        a.keep_package.write_bytes(pkg)

    logical = unpack_package(pkg)
    files = parse_ufw(logical)
    if "ota.bin" not in files:
        raise SystemExit(f"{tag}: no ota.bin in the package ({sorted(files)})")
    _, off, ln, crc = files["ota.bin"]
    ota = logical[off:off + ln]
    loader, load, attr, name = unwrap_ota(ota, lz4blk)
    print(f"loader: {name!r} attr 0x{attr:x}, load 0x{load:x}, {len(loader)} B")
    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.write_bytes(loader)
    print(f"wrote {a.out}")


if __name__ == "__main__":
    main()
