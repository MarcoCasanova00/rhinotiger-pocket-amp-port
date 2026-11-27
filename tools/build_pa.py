#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Pocket AMP device build: pi32v2 compile/link/package of the literal
RhinoTiger app on the Pocket AMP port.

Uses only the JieLi toolchain (JIELI_TOOLCHAIN) and the literal BSP headers in
base/bsp; no SDK driver source is compiled. Mirrors the canonical flow of
RhinoTiger's own base/tools/build.py (device build): the same flags, the same
link command, and the same app.bin packaging (text, then .ram_text and .data
at their load addresses).

Outputs in build/pa/: app.elf, app.bin, app.map, app.dis, text.bin,
ramtext.bin, data.bin.
"""

import os
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
APP_XIP = 0x02000120
APP_FLASH_BASE = 0x4000
MAX_IMAGE = 0x82000          # the store starts at 0x86000; image below 0x82000

TOOLCHAIN = Path(os.environ.get("JIELI_TOOLCHAIN", str(Path.home() / ".jieli/toolchain")))


def find_tc():
    for cand in (TOOLCHAIN,):
        if (cand / "pi32v2/bin/clang").exists():
            return cand
    raise SystemExit(f"build_pa: toolchain not found under {TOOLCHAIN} (JIELI_TOOLCHAIN)")


TC = find_tc()
CC = TC / "pi32v2/bin/clang"
LD = TC / "pi32v2/bin/ld"
OBJCOPY = TC / "common/bin/objcopy"
OBJDUMP = TC / "common/bin/objdump"

BSP_CHIP = ROOT / "base/bsp/chip"
BSP_BOARD = ROOT / "base/bsp/boards/pocketamp"
BASE_INC = ROOT / "base/include"
BASE_LIB = ROOT / "base/lib"
BASE_DEV = ROOT / "base/device"
APP = ROOT / "app"
GEN = ROOT / "app/build/gen"
DSP = ROOT / "app/dsp"
PLAT = ROOT / "plat/pocketamp"
LD_SCRIPT = ROOT / "ld/pocketamp.ld"
OUT = ROOT / "build/pa"

INC = [BSP_CHIP, BSP_BOARD, BASE_INC, BASE_LIB, BASE_DEV, APP, GEN, PLAT, DSP]
CFLAGS = [
    "-target", "pi32v2",
    "-mcpu=r3", "-mfprev1", "-ffp-contract=off",
    "-Os", "-ffunction-sections", "-fno-builtin", "-Wall", "-Wno-unused-function",
] + [f"-I{p}" for p in INC]
O2FLAGS = ["-O2" if f == "-Os" else f for f in CFLAGS]


def run(*cmd, cwd=None, out=None):
    print("+", " ".join(str(c) for c in cmd))
    p = subprocess.run([str(c) for c in cmd], cwd=cwd, capture_output=True, text=True)
    if p.returncode != 0:
        sys.stderr.write(p.stdout)
        sys.stderr.write(p.stderr)
        raise SystemExit(f"build_pa: failed: {' '.join(str(c) for c in cmd)}")
    if out is not None:
        Path(out).write_text(p.stdout)
    return p.stdout


def main():
    OUT.mkdir(parents=True, exist_ok=True)

    # assemble: crt0, vectors, ISR wrappers (the BSP's literal sources)
    objs = []
    for src in ("crt0.S", "fm1_vec.S", "fm1_isr.S"):
        o = OUT / (src.replace(".S", ".o"))
        run(CC, *CFLAGS, "-c", BSP_CHIP / src, "-o", o)
        objs.append(o)

    # the unity (main_pa.c includes unity.c), and the -O2 DSP unit
    uni = OUT / "main_pa.o"
    psy = OUT / "psy.o"
    run(CC, *CFLAGS, "-c", PLAT / "main_pa.c", "-o", uni)
    run(CC, *O2FLAGS, "-c", DSP / "psy.c", "-o", psy)
    objs += [uni, psy]

    elf = OUT / "app.elf"
    run(LD, "--gc-sections", "-e", "_start", "-Map", OUT / "app.map", "-T", LD_SCRIPT,
        *objs, "-o", elf)

    run(OBJCOPY, "-O", "binary", "-j", ".text", elf, OUT / "text.bin")
    run(OBJCOPY, "-O", "binary", "-j", ".data", elf, OUT / "data.bin")
    run(OBJCOPY, "-O", "binary", "-j", ".ram_text", elf, OUT / "ramtext.bin")
    syms = run(OBJDUMP, "-t", elf)
    run(OBJDUMP, "-d", "-mcpu=r3", elf, out=OUT / "app.dis")

    def symv(name):
        m = re.search(r"^([0-9a-f]+) .*\s" + re.escape(name) + r"$", syms, re.M)
        if not m:
            raise SystemExit(f"build_pa: symbol {name} not found")
        return int(m.group(1), 16)

    img = bytearray((OUT / "text.bin").read_bytes())
    for sect, lname in (("ramtext.bin", "_rt_load"), ("data.bin", "_data_load")):
        load = symv(lname)
        if load % 4:
            raise SystemExit(f"build_pa: {lname} {load:#x} not word aligned")
        blob = (OUT / sect).read_bytes()
        if blob:
            if load - APP_XIP < len(img):
                raise SystemExit(f"build_pa: {lname} overlaps the image")
            img += b"\xff" * (load - APP_XIP - len(img))
            img += blob
    img += b"\xff" * (-len(img) % 4)

    if len(img) > MAX_IMAGE:
        raise SystemExit(f"build_pa: image {len(img)} B > {MAX_IMAGE:#x} (the store's start)")
    (OUT / "app.bin").write_bytes(img)

    # a quick report
    bss_end = None
    for ln in syms.splitlines():
        if ln.rstrip().endswith("_bss_end"):
            bss_end = int(ln.split()[0], 16)
    print(f"build_pa: app.bin {len(img)} B (flash {APP_FLASH_BASE:#x} + {len(img):#x})")
    print(f"build_pa: _rt_load {symv('_rt_load'):#x}, _data_load {symv('_data_load'):#x}")
    print(f"build_pa: bss end {bss_end:#x}" if bss_end else "")


if __name__ == "__main__":
    main()
