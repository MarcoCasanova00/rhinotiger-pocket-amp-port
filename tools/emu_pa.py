#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Interactive emulator of the Pocket AMP control surface on the literal
RhinoTiger app (the host simulator), driven through the same virtual-panel
translator as the firmware (plat/pocketamp/pa_virt.c).

Usage:
  tools/emu_pa.py               interactive (keys below)
  tools/emu_pa.py --serial       line protocol for scripts:
      X <hex>            set the physical mask (PA_P_* bits)
      N <ms>             advance the translator clock
      H <cmd ...>        a raw host-simulator command (press, key, turn,
                         master, wait, expect, shot, fbdump, ...)
      RD                 read and print the host's pending output
      FB [path]          save the framebuffer (raw RGB565)
      Q                  quit

Interactive keys:
  q/e  PARA encoder left/right      p  PARA push toggle
  h    HOME toggle                  v  VOL- toggle
  b    VOL+ toggle                  ,  < toggle
  .    > toggle                     m/M  MASTER down/up (128 a step)
  s    redraw the screen            ESC or Ctrl-C  quit
"""

import os
import select
import shutil
import subprocess
import sys
import termios
import time
import tty
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TMP = Path(os.environ.get("OPENCODE_SESSION_ID") and "/tmp/opencode/paemu" or "/tmp/paemu")
HOST = ROOT / "app/build/host/rhinotiger_host"
HELPER = ROOT / "build/pa/pa_virt_cli"

WHITE = [0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26]
BLACK = [1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25]
BTN = ["FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ",
       "PLAY", "REC", "OCT-", "OCT+"]
ENC = ["SELECT", "ALGO", "PRESET", "K1", "K2", "K3", "K4"]


def build_helper():
    if HELPER.exists() and HELPER.stat().st_mtime > (ROOT / "plat/pocketamp/pa_virt.c").stat().st_mtime:
        return
    HELPER.parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(["gcc", "-Wall", "-Wextra",
                    f"-I{ROOT / 'plat/pocketamp'}", f"-I{ROOT / 'base/include'}",
                    "-o", str(HELPER),
                    str(ROOT / "tools/pa_virt_cli.c"),
                    str(ROOT / "plat/pocketamp/pa_virt.c")], check=True)


def build_host():
    if HOST.exists():
        return
    subprocess.run([str(ROOT / "tools/build_host.sh")], check=True)


class Emu:
    def __init__(self):
        build_helper()
        build_host()
        TMP.mkdir(parents=True, exist_ok=True)
        self.host = self._spawn_host()
        self.helper = subprocess.Popen([str(HELPER)], stdin=subprocess.PIPE,
                                       stdout=subprocess.PIPE, text=True)
        self.mask = 0
        self.now = 0
        self.host_now = 0
        self.last_btn = self.last_keys = 0
        self.last_e = [0] * len(ENC)
        self.master = 4096
        self.fb = None

    def _spawn_host(self):
        # script mode reads its script from argv[1]; /dev/stdin lets us drive
        # it; stdbuf makes its stdout live (script mode block-buffers by default)
        cmd = [str(HOST), "/dev/stdin", str(TMP)]
        if shutil.which("stdbuf"):
            cmd = ["stdbuf", "-oL"] + cmd
        return subprocess.Popen(cmd, stdin=subprocess.PIPE,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT)

    def _host(self, cmd):
        try:
            self.host.stdin.write(cmd.encode() + b"\n")
            self.host.stdin.flush()
        except (BrokenPipeError, ValueError):
            pass
        self.host_now += 1

    def _helper_run(self, enc=0):
        self.helper.stdin.write(f"D {self.mask:#x} {self.now} {enc} {self.master}\n")
        self.helper.stdin.flush()
        btn = int(self.helper.stdout.readline().split()[1], 16)
        keys = int(self.helper.stdout.readline().split()[1], 16)
        e = [int(x) for x in self.helper.stdout.readline().split()[1:]]
        return btn, keys, e

    def apply(self, force=False, enc=0):
        btn, keys, e = self._helper_run(enc)
        if force or btn != self.last_btn:
            diff = self.last_btn ^ btn
            for i in range(len(BTN)):
                if diff >> i & 1:
                    self._host(("press" if btn >> i & 1 else "release") + " " + BTN[i])
            self.last_btn = btn
        if force or keys != self.last_keys:
            diff = self.last_keys ^ keys
            for k in range(27):
                if diff >> k & 1:
                    if k in WHITE:
                        name = "w%d" % WHITE.index(k)
                    else:
                        name = "b%d" % BLACK.index(k)
                    self._host(("key %s down" if keys >> k & 1 else "key %s up") % name)
            self.last_keys = keys
        for i, n in enumerate(e):
            if n:
                self._host("turn %s %d" % (ENC[i], n))

    def tick(self, dt):
        self.now += dt
        self._host("wait %d" % dt)
        self.apply()

    def fb_save(self, name="fb.png"):
        p = TMP / name
        self._host(f"shot {name}")     # script mode: dir/name, relative
        self.fb = p

    def host_output(self):
        out = []
        while True:
            r, _, _ = select.select([self.host.stdout], [], [], 0)
            if not r:
                break
            chunk = os.read(self.host.stdout.fileno(), 65536)
            if not chunk:
                break
            out.extend(chunk.decode(errors="replace").splitlines())
        return out

    def close(self):
        for proc in (self.helper, self.host):
            if proc.poll() is None:
                proc.terminate()
        for proc in (self.helper, self.host):
            try:
                proc.wait(timeout=2)
            except Exception:
                proc.kill()


def render_fb(path):
    data = Path(path).read_bytes()[:240 * 240 * 2]
    px = []
    for i in range(0, len(data), 2):
        raw = data[i] | (data[i + 1] << 8)
        c = ((raw & 0xFF) << 8) | (raw >> 8)      # un-swap RGB565
        r = (c >> 11 & 31) << 3
        g = (c >> 5 & 63) << 2
        b = (c & 31) << 3
        px.append((r, g, b))
    rows = []
    for y in range(0, 240, 2):
        line = []
        for x in range(0, 240, 2):
            a = px[y * 240 + x]
            b = px[(y + 1) * 240 + x]
            c = px[y * 240 + x + 1]
            d = px[(y + 1) * 240 + x + 1]
            r = (a[0] + b[0] + c[0] + d[0]) // 4
            g = (a[1] + b[1] + c[1] + d[1]) // 4
            bl = (a[2] + b[2] + c[2] + d[2]) // 4
            line.append(f"\x1b[48;2;{r};{g};{bl}m ")
        rows.append("".join(line) + "\x1b[0m")
    return "\n".join(rows)


def serial_mode():
    e = Emu()
    try:
        for line in sys.stdin:
            line = line.strip()
            if not line:
                continue
            parts = line.split()
            cmd = parts[0]
            if cmd == "X":
                e.mask = int(parts[1], 16)
                e.apply()
            elif cmd == "ENC":
                e.apply(enc=int(parts[1]))
            elif cmd == "M":
                e.master = max(0, min(4096, int(parts[1])))
                e._host("master %d" % e.master)
                e.apply()
            elif cmd == "N":
                e.now += int(parts[1])
                e.apply()               # time-based effects (PARA holds) appear
            elif cmd == "H":
                e._host(" ".join(parts[1:]))
            elif cmd == "RD":
                deadline = time.time() + 0.6
                acc = []
                while time.time() < deadline:
                    acc += e.host_output()
                    time.sleep(0.05)
                if not acc:
                    print("(no host output)")
                for out in acc:
                    print(out)
                sys.stdout.flush()
            elif cmd == "FB":
                e.fb_save(parts[1] if len(parts) > 1 else None)
                print("fb ok")
                sys.stdout.flush()
            elif cmd == "Q":
                break
            else:
                print("unknown serial command: " + cmd)
    finally:
        e.close()


def interactive():
    e = Emu()
    held = {}
    try:
        old = termios.tcgetattr(sys.stdin.fileno())
        tty.setcbreak(sys.stdin.fileno())
    except Exception:
        old = None
    try:
        last_fb = 0
        last_tick = time.monotonic()
        print("Pocket AMP emulator: q/e PARA, p push, h HOME, v VOL-, b VOL+, , < . >, m/M MASTER, s screen, ESC quit")
        while True:
            now = time.monotonic()
            dt = int((now - last_tick) * 1000)
            if dt > 0:
                e.tick(min(dt, 500))
                last_tick = now
            if now - last_fb > 1.0:
                e.fb_save()
                last_fb = now
                print(render_fb(e.fb))
            r, _, _ = select.select([sys.stdin], [], [], 0.05)
            if not r:
                continue
            ch = sys.stdin.read(1)
            if ch == "\x1b":
                break
            k = ch.lower()
            if ch in "qQ":
                e.now += 10
                e.helper.stdin.write(f"D {e.mask:#x} {e.now} {'-1' if ch == 'q' else '1'} {e.master}\n")
                e.helper.stdin.flush()
                e.apply()
            elif k in "phvb,.":
                bit = {"p": 0, "h": 1, "v": 2, "b": 3, ",": 4, ".": 5}[k]
                held[k] = not held.get(k, False)
                if held[k]:
                    e.mask |= 1 << bit
                else:
                    e.mask &= ~(1 << bit)
                e.apply()
            elif k == "m":
                e.master = max(0, e.master - 128)
                e.helper.stdin.write(f"D {e.mask:#x} {e.now} 0 {e.master}\n")
                e.helper.stdin.flush()
                e._host("master %d" % e.master)
            elif ch == "M":
                e.master = min(4096, e.master + 128)
                e.helper.stdin.write(f"D {e.mask:#x} {e.now} 0 {e.master}\n")
                e.helper.stdin.flush()
                e._host("master %d" % e.master)
            elif k == "s":
                e.fb_save()
                print(render_fb(e.fb))
    finally:
        e.close()
        if old is not None:
            termios.tcsetattr(sys.stdin.fileno(), termios.TCSADRAIN, old)


if __name__ == "__main__":
    if "--serial" in sys.argv:
        serial_mode()
    else:
        interactive()
