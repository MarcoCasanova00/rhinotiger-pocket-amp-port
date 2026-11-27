#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Localhost browser demo of the literal RhinoTiger app on the Pocket AMP
control surface. Serves a page (canvas + virtual panel) and bridges it to the
host simulator through the firmware's own translator (pa_virt.c):
  - /         the page
  - /input    POST {mask, enc, master} -> {btn, keys} (diffs sent to the app)
  - /frame    GET raw RGB565 framebuffer (byte-swapped, 240x240)
  - /status   GET {page, playing} (polled from the app's expect)
  - /cmd      POST raw host commands (expect ...), returns replies
Run:  python3 tools/emu_pa_server.py [--port 8000]
Open:  http://localhost:8000/
"""

import argparse
import base64
import json
import sys
import threading
import time
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
from emu_pa import Emu, TMP  # noqa: E402

PAGE_NAME = ["MAIN", "GROOVE", "BREAK", "BUILD", "AUTO", "KICK", "BASS",
             "PERC", "EVENTS", "LINE", "LINE SEQ", "ATMOS", "MIX", "ECHO",
             "GLOBAL"]

emu = Emu()
lock = threading.Lock()
last_frame_mtime = 0
frame_cache = b""
audio_wav = TMP / "audio.wav"
audio_off = 44                       # skip the WAV header (zeros while open)
pkg_path = ROOT / "build/pa/pkg/rhinotiger-pa.fwsc"


def prewarm():
    with lock:
        emu._host("wait 300")
        emu._host("wav audio.wav")   # record the app's output (s16le stereo)
        emu._host("expect page")
        emu._host("wait 40")
        emu.host_output()


def pump():
    """Advance the simulated device clock at ~real time while idle, so the
    logo animates and the UI stays live between interactions."""
    while True:
        with lock:
            emu._host("wait 50")
        time.sleep(0.05)


prewarm()
threading.Thread(target=pump, daemon=True).start()


def refresh_frame():
    global last_frame_mtime, frame_cache
    p = TMP / "fb.png"
    try:
        m = p.stat().st_mtime
    except OSError:
        m = 0
    emu.fb_save("fb.png")
    data = b""
    for _ in range(50):                  # the dump lands asynchronously
        try:
            if p.stat().st_mtime != m or not data:
                data = p.read_bytes()
            if len(data) > 200 and data[:8] == b"\x89PNG\r\n\x1a\n":
                break
        except OSError:
            pass
        import time
        time.sleep(0.02)
    if data[:8] == b"\x89PNG\r\n\x1a\n":
        last_frame_mtime = p.stat().st_mtime
        frame_cache = data


class H(BaseHTTPRequestHandler):
    def log_message(self, *a):
        pass

    def _send(self, code, body, ctype="application/json"):
        if isinstance(body, str):
            body = body.encode()
        self.send_response(code)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-store")
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == "/" or self.path == "/index.html":
            self._send(200, PAGE, "text/html; charset=utf-8")
            return
        if self.path == "/frame":
            with lock:
                refresh_frame()
                body = frame_cache
            self._send(200, body, "image/png")
            return
        if self.path == "/status":
            page = playing = None
            with lock:
                emu.host_output()           # drop stale replies first
                for _ in range(8):          # replies land asynchronously
                    emu._host("expect page")
                    emu._host("expect playing")
                    emu._host("wait 20")
                    time.sleep(0.04)
                    for line in emu.host_output():
                        if "page" not in line and "playing" not in line:
                            continue
                        val = None
                        if "got " in line:  # FAIL ... got N
                            try:
                                val = int(line.rsplit("got ", 1)[1].strip())
                            except (IndexError, ValueError):
                                val = None
                        else:               # ok NAME == N
                            try:
                                val = int(line.split("==")[1].strip())
                            except (IndexError, ValueError):
                                val = None
                        if val is None:
                            continue
                        if "page" in line:
                            page = val
                        if "playing" in line:
                            playing = val
                    if page is not None and playing is not None:
                        break
            self._send(200, json.dumps({
                "page": page, "page_name": PAGE_NAME[page] if page is not None else None,
                "playing": playing, "host_now": emu.host_now,
            }))
            return
        if self.path == "/pkg-info":
            info = {"exists": False}
            try:
                st = pkg_path.stat()
                info = {"exists": True, "name": pkg_path.name, "size": st.st_size}
            except OSError:
                pass
            self._send(200, json.dumps(info))
            return
        if self.path == "/pkg":
            try:
                data = pkg_path.read_bytes()
            except OSError:
                self._send(404, "no package — run tools/pa_pkg.sh", "text/plain")
                return
            self._send(200, data, "application/octet-stream")
            return
        if self.path == "/audio":
            global audio_off
            with lock:
                data = b""
                try:
                    size = audio_wav.stat().st_size
                except OSError:
                    size = 0
                if size > audio_off:
                    with open(audio_wav, "rb") as f:
                        f.seek(audio_off)
                        data = f.read(size - audio_off)
                    audio_off = size
            self._send(200, data, "application/octet-stream")
            return
        if self.path == "/leds":
            with lock:
                for _ in range(6):
                    emu._host("leds")
                    emu._host("wait 20")
                    time.sleep(0.04)
                    r = emu.host_output()
                    line = next((x for x in reversed(r) if "leds:" in x), None)
                    if line:
                        break
            buttons, white, black = [], "", ""
            if line:
                try:
                    rest = line.split("buttons", 1)[1]
                    left, wb = rest.split("| white ", 1)
                    buttons = left.split()
                    wpart, black = wb.split(" black ", 1)
                    white = wpart.strip()
                except ValueError:
                    pass
            self._send(200, json.dumps({"buttons": buttons, "white": white,
                                         "black": black[:11]}))
            return
        if self.path == "/input":
            self._send(405, json.dumps({"error": "POST only"}))
            return
        self._send(404, "not found", "text/plain")

    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0))
        raw = self.rfile.read(n) if n else b"{}"
        try:
            d = json.loads(raw or b"{}")
        except ValueError:
            self._send(400, json.dumps({"error": "bad json"}))
            return
        if self.path == "/input":
            with lock:
                if "mask" in d:
                    emu.mask = int(d["mask"], 16) & 0x3F
                if "enc" in d:
                    emu.apply(enc=int(d["enc"]))
                if "master" in d:
                    emu.master = max(0, min(4096, int(d["master"])))
                    emu._host("master %d" % emu.master)
                emu.apply()
                emu._host("wait 30")
            self._send(200, json.dumps({"btn": emu.last_btn, "keys": emu.last_keys}))
            return
        if self.path == "/cmd":
            with lock:
                for c in d.get("cmd", []):
                    emu._host(c)
                emu._host("wait 30")
                r = emu.host_output()
            self._send(200, json.dumps({"replies": r}))
            return
        self._send(404, json.dumps({"error": "unknown"}))


PAGE = r"""<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>RhinoTiger · Pocket AMP port</title>
<link rel="icon" href="data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHZpZXdCb3g9IjAgMCAxMjAgNjQiPgogIDxyZWN0IHg9IjEuNSIgeT0iMS41IiB3aWR0aD0iMTE3IiBoZWlnaHQ9IjYxIiByeD0iMTIiIGZpbGw9IiMyNjI2MmEiIHN0cm9rZT0iIzAwMCIgc3Ryb2tlLXdpZHRoPSIzIi8+CiAgPHJlY3QgeD0iMTQiIHk9IjgiIHdpZHRoPSI5MiIgaGVpZ2h0PSIyNCIgcng9IjUiIGZpbGw9IiMwNTA1MDUiLz4KICA8cmVjdCB4PSIxNyIgeT0iMTEiIHdpZHRoPSI4NiIgaGVpZ2h0PSIxOCIgcng9IjMiIGZpbGw9IiMwZDBkMTAiLz4KICA8cmVjdCB4PSIxNyIgeT0iMjQiIHdpZHRoPSI4NiIgaGVpZ2h0PSIzIiBmaWxsPSIjZjI4YTVhIiBvcGFjaXR5PSIwLjUiLz4KICA8Y2lyY2xlIGN4PSIyMiIgY3k9IjQ3IiByPSI4IiBmaWxsPSIjM2EzYTNlIiBzdHJva2U9IiMwMDAiIHN0cm9rZS13aWR0aD0iMS41Ii8+CiAgPGNpcmNsZSBjeD0iMjIiIGN5PSI0NyIgcj0iMy40IiBmaWxsPSIjMWIxYjFlIi8+CiAgPHJlY3QgeD0iMjIiIHk9IjM4IiB3aWR0aD0iMi40IiBoZWlnaHQ9IjgiIHJ4PSIxLjIiIGZpbGw9IiNmMjhhNWEiLz4KICA8cmVjdCB4PSI0MiIgeT0iNDMiIHdpZHRoPSI5IiBoZWlnaHQ9IjkiIHJ4PSIyIiBmaWxsPSIjMmUyZTMyIiBzdHJva2U9IiMwMDAiIHN0cm9rZS13aWR0aD0iMSIvPgogIDxjaXJjbGUgY3g9IjQ2LjUiIGN5PSI0My40IiByPSIxLjMiIGZpbGw9IiNmMjhhNWEiLz4KICA8cmVjdCB4PSI1NSIgeT0iNDMiIHdpZHRoPSI5IiBoZWlnaHQ9IjkiIHJ4PSIyIiBmaWxsPSIjMmUyZTMyIiBzdHJva2U9IiMwMDAiIHN0cm9rZS13aWR0aD0iMSIvPgogIDxjaXJjbGUgY3g9IjU5LjUiIGN5PSI0My40IiByPSIxLjMiIGZpbGw9IiMyYTIzMjAiLz4KICA8cmVjdCB4PSI2OCIgeT0iNDMiIHdpZHRoPSI5IiBoZWlhaHQ9IjkiIHJ4PSIyIiBmaWxsPSIjMmUyZTMyIiBzdHJva2U9IiMwMDAiIHN0cm9rZS13aWR0aD0iMSIvPgogIDxjaXJjbGUgY3g9IjcyLjUiIGN5PSI0My40IiByPSIxLjMiIGZpbGw9IiMyYTIzMjAiLz4KICA8cmVjdCB4PSI4MSIgeT0iNDMiIHdpZHRoPSI5IiBoZWlnaHQ9IjkiIHJ4PSIyIiBmaWxsPSIjMmUyZTMyIiBzdHJva2U9IiMwMDAiIHN0cm9rZS13aWR0aD0iMSIvPgogIDxjaXJjbGUgY3g9Ijg1LjUiIGN5PSI0My40IiByPSIxLjMiIGZpbGw9IiMyYTIzMjAiLz4KICA8Y2lyY2xlIGN4PSIxMDQiIGN5PSI0NyIgcj0iOCIgZmlsbD0iIzNhM2EzZSIgc3Ryb2tlPSIjMDAwIiBzdHJva2Utd2lkdGg9IjEuNSIvPgogIDxjaXJjbGUgY3g9IjEwNCIgY3k9IjQ3IiByPSIzLjQiIGZpbGw9IiMxYjFiMWUiLz4KICA8cmVjdCB4PSI5OCIgeT0iMzgiIHdpZHRoPSIyLjQiIGhlaWdodD0iOCIgcng9IjEuMiIgZmlsbD0iI2YyOGE1YSIvPgo8L3N2Zz4K">
<style>
:root{--ground:#1c1814;--ink:#f2e8d8;--muted:#b3a28e;--rule:#3a322a;--card:#26201b;
      --accent:#f28a5a;--teal:#4cc3b6;--bad:#ff8a73;--focus:#79adff;--alum:#232324;--alum2:#2c2c2f;}
*{box-sizing:border-box}
body{background:radial-gradient(1200px 600px at 50% -10%, #2a241d, var(--ground));color:var(--ink);
    margin:0;padding:28px 18px 60px;font:400 15px/1.5 "Barlow","Segoe UI",system-ui,sans-serif}
main{max-width:1040px;margin:0 auto}
h1{font:700 1.9rem/1.1 "Barlow Semi Condensed","Barlow",sans-serif;margin:0;letter-spacing:.02em}
h1 em{font-style:normal;color:var(--accent)}
header{display:flex;flex-wrap:wrap;gap:.4rem 1.2rem;align-items:center;margin:0 0 1rem}
header p{margin:0;color:var(--muted)}
.devicon{width:58px;height:auto;filter:drop-shadow(0 4px 10px #0008);flex:0 0 auto}
.row{display:flex;flex-wrap:wrap;gap:.6rem 1.4rem;align-items:center}
.chip{font:600 .8rem ui-monospace,monospace;padding:.2em .7em;border:1px solid var(--rule);border-radius:999px;color:var(--muted)}
.chip b{color:var(--ink);font-weight:600}
.chip.on{border-color:var(--accent);color:var(--accent)}
/* --- the Pocket AMP case --- */
.pa{position:relative;border-radius:20px;padding:22px 26px 24px;background:linear-gradient(180deg,#2a2a2d,#161618 30%,#101012);
    border:1px solid #000;box-shadow:0 18px 50px #0009, inset 0 1px 0 #ffffff14}
.pa .model{position:absolute;top:10px;right:16px;color:#55524c;font:600 .68rem ui-monospace,monospace;letter-spacing:.14em}
.pa .model b{color:#7a7266}
/* screen */
.scr{background:#050505;border-radius:12px;padding:12px;border:1px solid #2a2a2e;box-shadow:inset 0 0 30px #000, 0 2px 0 #ffffff08}
.scr .glass{border-radius:6px;overflow:hidden;background:#000;position:relative}
canvas{display:block;width:280px;height:280px;margin:0 auto;image-rendering:pixelated}
.scr .glass::after{content:"";position:absolute;inset:0;background:linear-gradient(115deg,#ffffff08 0 40%,transparent 40% 60%,#ff9a3c07 60% 100%);pointer-events:none}
.scr .cap{color:var(--muted);font:600 .7rem ui-monospace,monospace;letter-spacing:.2em;text-align:center;padding-top:8px;text-transform:uppercase}
/* the control row */
.rowctl{display:flex;align-items:center;gap:14px;margin-top:20px;flex-wrap:wrap;justify-content:center}
.knobmod{display:flex;flex-direction:column;align-items:center;gap:6px;min-width:110px}
.knobmod .lbl{font:600 .72rem "Barlow Semi Condensed","Barlow",sans-serif;letter-spacing:.14em;color:var(--muted);text-transform:uppercase}
.knob{width:76px;height:76px;border-radius:50%;position:relative;cursor:ns-resize;touch-action:none;
      background:conic-gradient(#3a3a3e,#17171a,#3a3a3e,#17171a,#3a3a3e);border:1px solid #000;
      box-shadow:0 6px 14px #000a, inset 0 1px 0 #ffffff22}
.knob::after{content:"";position:absolute;inset:14px;border-radius:50%;background:linear-gradient(180deg,#2b2b2f,#19191c);box-shadow:inset 0 1px 0 #ffffff18}
.knob::before{content:"";position:absolute;top:5px;left:50%;width:4px;height:15px;background:var(--accent);
              transform:translateX(-50%) rotate(var(--rot,0deg));transform-origin:50% 33px;border-radius:2px;transition:transform .06s}
.knob.held{box-shadow:0 6px 14px #000a, inset 0 0 0 2px #f28a5a44}
.pads{display:flex;gap:10px}
.pad{position:relative;width:64px;padding:18px 4px 12px;text-align:center;cursor:pointer;user-select:none;touch-action:manipulation;
     background:linear-gradient(180deg,var(--alum2),var(--alum));border:1px solid #000;border-radius:9px;
     box-shadow:0 4px 0 #000, inset 0 1px 0 #ffffff14;font:600 .82rem "Barlow Semi Condensed","Barlow",sans-serif}
.pad .n{position:absolute;top:5px;left:50%;transform:translateX(-50%);width:7px;height:7px;border-radius:50%;background:#1f1c18;box-shadow:inset 0 0 2px #000}
.pad.on{background:linear-gradient(180deg,#8a4a22,#6e3a1a);box-shadow:0 4px 0 #000, inset 0 0 14px #ff9a3c33}
.pad.on .n{background:var(--accent);box-shadow:0 0 10px var(--accent)}
.pad.home{width:74px}
/* internal (FM-1) state: a debug drawer, not the device */
details.db{margin-top:16px;border:1px solid var(--rule);border-radius:12px;background:var(--card);padding:8px 12px}
details.db summary{cursor:pointer;font:600 .8rem "Barlow Semi Condensed","Barlow",sans-serif;letter-spacing:.12em;color:var(--muted);text-transform:uppercase}
.leds{display:flex;flex-wrap:wrap;gap:5px;margin-top:8px}
.vled{flex:1 1 56px;min-width:56px;padding:4px 2px;text-align:center;font:600 .6rem ui-monospace,monospace;color:var(--muted);
      border:1px solid var(--rule);border-radius:6px;background:#1a1713}
.vled.on{color:#ffd9b3;border-color:var(--accent);background:#3a2412;box-shadow:0 0 8px #f28a5a44}
.keybd{display:flex;height:30px;border-radius:4px;overflow:hidden;border:1px solid #000;margin-top:8px}
.kw{flex:1;background:linear-gradient(180deg,#e8e4da,#b9b4a8);border-right:1px solid #6a645a}
.kw.on{background:linear-gradient(180deg,#ffc07a,#e08a3a)}
.kb{height:18px;background:#201d19;border-left:1px solid #000}
.kb.on{background:var(--accent)}
/* help */
.help{display:grid;grid-template-columns:repeat(auto-fit,minmax(300px,1fr));gap:1rem 2rem;margin-top:1.4rem}
.help h2{font:600 1rem "Barlow Semi Condensed","Barlow",sans-serif;margin:0 0 .3rem;color:var(--muted);letter-spacing:.08em;text-transform:uppercase}
table{border-collapse:collapse;width:100%}
td,th{padding:.25rem .5rem;border-bottom:1px solid var(--rule);text-align:left;font-size:.92rem}
th{color:var(--muted);font-weight:600}
kbd{font:600 .82em ui-monospace,monospace;background:var(--card);border:1px solid var(--rule);border-radius:4px;padding:0 .3em}
#alert{color:var(--bad);font-weight:600;min-height:1.2em}
.start{position:absolute;inset:0;display:grid;place-items:center;background:rgba(10,11,13,.65);border-radius:20px;z-index:5}
.start[hidden]{display:none}
.start .boot{font:600 1.1rem "Barlow Semi Condensed","Barlow",sans-serif;letter-spacing:.1em;color:var(--accent)}
@media (max-width:700px){ canvas{width:100%;height:auto} .rowctl{gap:10px} .pad{width:56px} .knob{width:64px;height:64px} }
</style></head><body>
<main>
<header>
 <svg class="devicon" viewBox="0 0 120 64" aria-hidden="true">
  <rect x="1.5" y="1.5" width="117" height="61" rx="12" fill="#26262a" stroke="#000" stroke-width="3"/>
  <rect x="14" y="8" width="92" height="24" rx="5" fill="#050505"/>
  <rect x="17" y="11" width="86" height="18" rx="3" fill="#0d0d10"/>
  <rect x="17" y="24" width="86" height="3" fill="#f28a5a" opacity="0.5"/>
  <circle cx="22" cy="47" r="8" fill="#3a3a3e" stroke="#000" stroke-width="1.5"/>
  <circle cx="22" cy="47" r="3.4" fill="#1b1b1e"/>
  <rect x="22" y="38" width="2.4" height="8" rx="1.2" fill="#f28a5a"/>
  <rect x="42" y="43" width="9" height="9" rx="2" fill="#2e2e32" stroke="#000" stroke-width="1"/>
  <circle cx="46.5" cy="43.4" r="1.3" fill="#f28a5a"/>
  <rect x="55" y="43" width="9" height="9" rx="2" fill="#2e2e32" stroke="#000" stroke-width="1"/>
  <circle cx="59.5" cy="43.4" r="1.3" fill="#2a2320"/>
  <rect x="68" y="43" width="9" height="9" rx="2" fill="#2e2e32" stroke="#000" stroke-width="1"/>
  <circle cx="72.5" cy="43.4" r="1.3" fill="#2a2320"/>
  <rect x="81" y="43" width="9" height="9" rx="2" fill="#2e2e32" stroke="#000" stroke-width="1"/>
  <circle cx="85.5" cy="43.4" r="1.3" fill="#2a2320"/>
  <circle cx="104" cy="47" r="8" fill="#3a3a3e" stroke="#000" stroke-width="1.5"/>
  <circle cx="104" cy="47" r="3.4" fill="#1b1b1e"/>
  <rect x="98" y="38" width="2.4" height="8" rx="1.2" fill="#f28a5a"/>
 </svg>
 <h1>RhinoTiger <em>· Pocket AMP</em></h1>
 <p>the port — in the browser</p>
 <div class="row" style="margin-left:auto">
  <span class="chip" id="chip-page">page —</span>
  <span class="chip" id="chip-play">—</span>
  <span class="chip" id="chip-ms">0 ms</span>
  <span class="chip on">ON</span>
 </div>
</header>

<div class="pa">
 <div class="model">M-VAVE / Donner <b>Pocket AMP</b></div>
 <div class="scr">
  <div class="glass"><canvas id="cv" width="240" height="240"></canvas></div>
  <div class="cap">RhinoTiger screen</div>
 </div>
 <div class="rowctl">
  <div class="knobmod">
   <div class="knob" id="knob-para"></div>
   <div class="lbl">PARA · push</div>
  </div>
  <div class="pads">
   <span class="pad home" data-b="1" id="home">HOME<span class="n"></span></span>
   <span class="pad" data-b="2" id="vold">VOL−<span class="n"></span></span>
   <span class="pad" data-b="3" id="volu">VOL+<span class="n"></span></span>
   <span class="pad" data-b="4" id="left">&lt;<span class="n"></span></span>
   <span class="pad" data-b="5" id="right">&gt;<span class="n"></span></span>
  </div>
  <div class="knobmod">
   <div class="knob" id="knob-master"></div>
   <div class="lbl">MASTER <span id="mval" style="color:var(--accent)">100%</span></div>
  </div>
 </div>
 <div class="start" id="start"><span class="boot">starting…</span></div>
</div>

<div class="row" style="margin-top:.9rem">
 <span id="alert"></span>
 <span style="color:var(--muted);font-size:.9rem">drag the knobs up/down · keyboard: <kbd>P</kbd> push ·
  <kbd>Q</kbd>/<kbd>E</kbd> turn · <kbd>H</kbd> HOME · <kbd>V</kbd>/<kbd>B</kbd> VOL−/+ ·
  <kbd>,</kbd>/<kbd>.</kbd> &lt;/&gt; · <kbd>M</kbd>/<kbd>N</kbd> MASTER · <b style="color:var(--teal)">click anywhere to start the sound</b></span>
</div>

<details class="db">
 <summary>see the app's FM-1 internals (debug)</summary>
 <div class="leds" id="leds"></div>
 <div class="keybd" id="keybd"></div>
</details>

<div class="help">
 <div><h2>chords</h2><table>
  <tr><td>VOL−</td><td>PLAY</td></tr>
  <tr><td>VOL+</td><td>ARP</td></tr>
  <tr><td>&lt;</td><td>SAVE</td></tr>
  <tr><td>&gt;</td><td>REC</td></tr>
  <tr><td>VOL−+VOL+</td><td>HOME</td></tr>
  <tr><td>&lt;+&gt;</td><td>SEQ</td></tr>
  <tr><td>VOL−+&lt;</td><td>SEL</td></tr>
  <tr><td>VOL++&gt;</td><td>ENV</td></tr>
  <tr><td>VOL−+&gt;</td><td>LFO</td></tr>
  <tr><td>VOL++&lt;</td><td>EDIT</td></tr>
  <tr><td>HOME+VOL−</td><td>GLO</td></tr>
  <tr><td>HOME+VOL+</td><td>FX</td></tr>
  <tr><td>HOME+&lt; / &gt;</td><td>OCT− / OCT+</td></tr>
 </table></div>
 <div><h2>PARA</h2><table>
  <tr><th>click</th><td>cycles PAGE · WORLD · K1..K4 · ALGO · KEY</td></tr>
  <tr><th>turn</th><td>the focus's knob</td></tr>
  <tr><th>hold 1 s</th><td>GLOBAL, then SAVE on release</td></tr>
  <tr><th>hold 3 s</th><td>GLOBAL + SAVE held → factory defaults</td></tr>
  <tr><th>KEY focus</th><td>VOL−/VOL+ play the cursor, &lt;/&gt; move it</td></tr>
 </table></div>
</div>

<details class="db" id="flashbox">
 <summary>install the port on a real Pocket AMP (browser flasher)</summary>
 <div class="row" style="margin-top:8px">
  <button class="mini" id="fl-detect" type="button">Detect Pocket AMP</button>
  <button class="mini" id="fl-install" type="button" disabled>Install rhinotiger-pa.fwsc</button>
 </div>
 <div id="fl-status" style="color:var(--muted);font-size:.9rem;margin-top:8px">package: checking…</div>
 <div class="row" style="margin-top:6px">
  <span style="color:var(--muted);font-size:.85rem">two-step updater over Web MIDI (Chrome/Edge). Back up presets first
   (pocket-amp-tools). Do not unplug while writing. Back to stock: install the official Pocket AMP.fwsc the same way.</span>
 </div>
</details>
</main>

<script>
"use strict";
const cv=document.getElementById('cv'),ctx=cv.getContext('2d');
let mask=0, master=4096, firstFrame=false, rot=0;
const BTN=["FX","SEL","ENV","LFO","EDIT","GLO","HOME","SAVE","ARP","SEQ","PLAY","REC","OCT-","OCT+"];
const body=(o)=>({method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(o)});
async function api(path,obj){const r=await fetch(path,obj?body(obj):{}); return r.json();}
function sendMask(){api('/input',{mask:mask.toString(16)});}
function enc(d){api('/input',{enc:d});}
function setMaster(v){master=v;api('/input',{master:v});}
function bit(b,on){if(on)mask|=1<<b; else mask&=~(1<<b);
 const el=document.querySelector('[data-b="'+b+'"]'); if(el)el.classList.toggle('on',on); sendMask();}
document.querySelectorAll('.pad[data-b]').forEach(el=>{
 const b=+el.dataset.b;
 el.addEventListener('pointerdown',e=>{e.preventDefault();bit(b,true);});
 ['pointerup','pointerleave','pointercancel'].forEach(ev=>el.addEventListener(ev,()=>bit(b,false)));
});
/* knob drag: PARA turns (and push on click), MASTER sets the level */
function clampV(v){return Math.max(0,Math.min(4096,v));}
function spinKnob(el,kind){
 let deg;
 if(kind==='master')deg=-135+master/4096*270;
 else deg=rot%360;
 el.style.setProperty('--rot',deg+'deg');
}
function updateMasterLabel(){
 const m=document.getElementById('mval'); if(m)m.textContent=Math.round(master/4096*100)+'%';
}
function attachKnob(id, kind){
 const el=document.getElementById(id); let down=false, y0=0, acc=0;
 el.addEventListener('pointerdown',e=>{e.preventDefault();down=true;y0=e.clientY;acc=0;el.classList.add('held');});
 window.addEventListener('pointermove',e=>{
  if(!down)return;
  const dy=y0-e.clientY; y0=e.clientY;
  if(kind==='para'){
   acc+=dy;
   while(acc>=12){enc(1);rot+=18;acc-=12;}
   while(acc<=-12){enc(-1);rot-=18;acc+=12;}
   if(acc!==0){acc=0;}
   spinKnob(el,'para');
  } else {
   setMaster(clampV(master+dy*24));
   spinKnob(el,'master');
  }
 });
 window.addEventListener('pointerup',()=>{if(down){down=false;el.classList.remove('held');}});
 el.addEventListener('wheel',e=>{e.preventDefault();
  if(kind==='para')enc(e.deltaY<0?1:-1);
  else {setMaster(clampV(master+(e.deltaY<0?128:-128)));spinKnob(el,'master');}
 },{passive:false});
}
attachKnob('knob-para','para');
attachKnob('knob-master','master');
document.getElementById('knob-para').addEventListener('pointerdown',()=>bit(0,true));
document.getElementById('knob-para').addEventListener('pointerup',()=>bit(0,false));
setInterval(updateMasterLabel,500); updateMasterLabel();
window.addEventListener('keydown',e=>{
 const k=e.key.toLowerCase(); const map={'p':0,'h':1,'v':2,'b':3,'q':4,',':4,'.':5};
 if(k==='q'||k==='e')enc(k==='q'?-1:1);
 else if(k==='m'||k==='n')setMaster(Math.max(0,Math.min(4096,master+(k==='m'?-128:128))));
 else if(k in map&&!e.repeat)bit(map[k],true);
});
window.addEventListener('keyup',e=>{const k=e.key.toLowerCase();const map={'p':0,'h':1,'v':2,'b':3,'q':4,',':4,'.':5};if(k in map)bit(map[k],false);});

function drawLeds(btns,white,black){
 const l=document.getElementById('leds'); l.innerHTML='';
 BTN.forEach(n=>{
  const d=document.createElement('div'); d.className='vled'; d.textContent=n;
  if(btns.includes(n))d.classList.add('on');
  l.appendChild(d);
 });
 const kb=document.getElementById('keybd'); kb.innerHTML='';
 const W=[0,2,4,6,7,9,11,12,14,16,18,19,21,23,24,26], B=[1,3,5,8,10,13,15,17,20,22,25];
 for(let k=0;k<27;k++){
  const i=W.indexOf(k);
  if(i>=0){
   const d=document.createElement('div'); d.className='kw';
   if(white&&white[i]==='#')d.classList.add('on'); kb.appendChild(d);
  } else {
   const j=B.indexOf(k);
   const d=document.createElement('div'); d.className='kb';
   if(black&&black[j]==='#')d.classList.add('on'); kb.appendChild(d);
  }
 }
}
async function leds(){
 try{ const s=await api('/leds'); if(s.buttons)drawLeds(s.buttons,s.white,s.black); }catch(e){}
}
async function frame(){
 try{
  const r=await fetch('/frame'); if(r.status!==200)return;
  const blob=await r.blob(); const img=new Image();
  img.onload=()=>{
   ctx.drawImage(img,0,0,240,240);
   if(!firstFrame){firstFrame=true;document.getElementById('start').hidden=true;}
  };
  img.src=URL.createObjectURL(blob);
 }catch(e){}
}
let retry=0;
async function status(){
 let s=null;
 try{s=await api('/status');}catch(e){}
 if(s&&s.page!==null){
  retry=0;
  document.getElementById('chip-page').innerHTML='page <b>'+(s.page_name??'?')+'</b> ('+s.page+')';
  const cp=document.getElementById('chip-play');
  cp.innerHTML=s.playing?'<b>PLAYING</b>':'<b>resting</b>';
  cp.classList.toggle('on',!!s.playing);
  document.getElementById('chip-ms').textContent=s.host_now+' ms';
 } else {
  retry++;
  document.getElementById('alert').textContent=
   retry>12?'server unreachable — is the WSL terminal still open?':'';
 }
}
setInterval(frame,400); setInterval(status,700); setInterval(leds,700);
frame(); status(); leds();

/* audio: the app's rendered output streamed from the host's WAV recording */
let actx=null, nextT=0;
function audioInit(){
 if(!actx){
  const AC=window.AudioContext||window.webkitAudioContext;
  if(!AC)return;
  actx=new AC(); nextT=actx.currentTime+0.1;
 }
 if(actx.state==='suspended')actx.resume();
}
async function pullAudio(){
 if(!actx)return;
 try{
  const r=await fetch('/audio'); if(r.status!==200)return;
  const buf=await r.arrayBuffer(); if(!buf.byteLength)return;
  const n=buf.byteLength/4; const i16=new Int16Array(buf);
  const ab=actx.createBuffer(2,n,44100);
  const L=ab.getChannelData(0),R=ab.getChannelData(1);
  for(let i=0;i<n;i++){L[i]=i16[2*i]/32768;R[i]=i16[2*i+1]/32768;}
  const src=actx.createBufferSource(); src.buffer=ab; src.connect(actx.destination);
  if(nextT<actx.currentTime+0.05)nextT=actx.currentTime+0.05;
  src.start(nextT); nextT+=n/44100;
 }catch(e){}
}
['pointerdown','keydown'].forEach(ev=>window.addEventListener(ev,audioInit));
setInterval(pullAudio,300);
</script>

<script>
/* ---- browser flasher: install the .fwsc over Web MIDI (the two-step
   updater the stock Pocket AMP speaks). Protocol equivalents of Pendel's
   pa_install.py, written fresh for this page. ---- */
(function(){
 "use strict";
 const $=id=>document.getElementById(id);
 let midi=null, outPort=null, inPort=null, frameQ=[], waiters=[];
 const HS_QUERY=[0xF0,0x00,0x32,0x45,0x00,0x00,0x00,0x40,0x7F,0xF7];
 const UPGRADE=[0xF0,0x22,0x24,0x35,0x7F,0xF7];
 const FINISH_CHECK=0xE0000000, FINISH_WRITE=0xF0000000;
 const PRODUCT="Pocket AMP_900";

 function fmt(x){return "0x"+x.toString(16);}
 function sleep(ms){return new Promise(r=>setTimeout(r,ms));}

 function pack7(data){
  const out=[]; let acc=0, nb=0;
  for(const b of data){ acc|=b<<nb; nb+=8; while(nb>=7){ out.push(acc&0x7F); acc>>=7; nb-=7; } }
  if(nb) out.push(acc&0x7F);
  return out;
 }
 function unpack7(s){
  const out=[]; let acc=0, nb=0;
  for(const b of s){ acc|=(b&0x7F)<<nb; nb+=7; while(nb>=8){ out.push(acc&0xFF); acc>>=8; nb-=8; } }
  return out;
 }
 function buildResponse(addr, data, fl=0){
  const n=data.length, body=[0x00,0x59,0x30,(n+8)&0xFF,((n+8)>>8)&0xFF,0,fl];
  const a=[addr&0xFF,(addr>>8)&0xFF,(addr>>16)&0xFF,(addr>>24)&0xFF];
  body.push(...a, n&0xFF,(n>>8)&0xFF,0, ...data);
  let sum=0; for(let i=6;i<body.length;i++) sum+=body[i];
  body.push((~sum)&0xFF);
  return [0xF0, ...pack7(body), 0xF7];
 }
 function parseRequest(pkt){
  if(pkt.length<2||pkt[0]!==0xF0||pkt[pkt.length-1]!==0xF7) return null;
  const u=unpack7(pkt.slice(1,-1));
  if(u.length!==15||u[0]!==0x00||u[1]!==0x59||u[2]!==0x30) return null;
  let sum=0; for(let i=6;i<14;i++) sum+=u[i];
  if(((~sum)&0xFF)!==u[14]) return null;
  const addr=u[6]|(u[7]<<8)|(u[8]<<16)|(u[9]<<24);
  const len=u[10]|(u[11]<<8)|(u[12]<<16);
  return [u[6], addr, len];
 }
 function parseIdentity(pkt){
  if(pkt.length<2||pkt[0]!==0xF0||pkt[pkt.length-1]!==0xF7) return null;
  const d=unpack7(pkt.slice(1,-1));
  if(d.length!==34||d[0]!==0x00||d[1]!==0x59||d[2]!==0x11) return null;
  let txt=""; for(let i=6;i<33;i++){ const c=d[i]; txt+= (c>=32&&c<127)?String.fromCharCode(c):"?"; }
  txt=txt.replace(/\0+$/,"");
  const m=txt.match(/^([^_]+)_(\d+)/);
  return m?{text:txt,model:m[1],version:+m[2],loader:m[1].toLowerCase().startsWith("ota-")}:null;
 }

 function send(pkt){ if(outPort) outPort.send(Array.from(pkt.slice(1,-1))); }
 function onFrame(f){ const w=waiters.shift(); if(w){clearTimeout(w.t); w.res(f);} else frameQ.push(f); }
 function awaitFrame(ms){ return new Promise(res=>{ const t=setTimeout(()=>{ waiters.splice(waiters.indexOf(w),1); res(null); },ms); const w={res,t}; waiters.push(w); }); }

 async function midiInit(){
  if(midi) return true;
  try{ midi=await navigator.requestMIDIAccess({sysex:true}); }catch(e){ return false; }
  for(const p of midi.outputs.values()){ if(/pocket|pendel|ota|composite|usb-midi/i.test(p.name)){ outPort=p; break; } }
  for(const p of midi.inputs.values()){ if(/pocket|pendel|ota|composite|usb-midi/i.test(p.name)){ inPort=p; break; } }
  if(inPort) inPort.onmidimessage=ev=>{ const d=ev.data; if(d&&d[0]===0xF0&&d[d.length-1]===0xF7) onFrame(new Uint8Array(d)); };
  return !!(outPort&&inPort);
 }

 async function find(want, secs, onlyPort){
  const end=Date.now()+secs*1000;
  while(Date.now()<end){
   if(!(await midiInit())) return null;
   const q=[...frameQ]; frameQ.length=0;
   send(HS_QUERY);
   const t=Date.now()+1200;
   while(Date.now()<t){
    const f=q.length?q.shift():await awaitFrame(500);
    if(!f) continue;
    const id=parseIdentity(f);
    if(id&&(!want||want(id))) return id;
   }
   await sleep(500);
  }
  return null;
 }
 async function serve(image, finish, idleMs, onProgress){
  let served=0, last=Date.now();
  for(;;){
   const pkt=await awaitFrame(1000);
   if(!pkt){ if(Date.now()-last>idleMs) return {ok:false,why:"idle"}; continue; }
   const r=parseRequest(pkt); if(!r) continue;
   last=Date.now();
   const addr=r[1], n=r[2];
   if(addr===FINISH_CHECK||addr===FINISH_WRITE){
    send(buildResponse(addr,[0x73,0x75,0x63,0x63,0x65,0x73,0x73,0x00]));
    if(addr===finish) return {ok:true};
    continue;
   }
   if(n>512||addr+n>image.length) return {ok:false,why:"bad request"};
   await sleep(10);
   send(buildResponse(addr, image.subarray(addr,addr+n), r[0]));
   served++; if(onProgress) onProgress(served, addr+n);
  }
 }

 function unpackPackage(raw){
  if(raw.length<20*0x30) throw new Error("not a package");
  const out=new Uint8Array(raw.length-20);
  let o=0;
  for(let i=0;i<20;i++){ out.set(raw.subarray(i*0x30,i*0x30+0x2F),o); o+=0x2F; }
  out.set(raw.subarray(20*0x30),o);
  return out;
 }
 function enc(buf,off,size,key){ key=key||0xFFFF; for(let i=0;i<size;i++){ buf[off+i]^=key&0xFF; key=((key<<1)^(key&0x8000?0x1021:0))&0xFFFF; } }
 function parseUfw(logical){
  const hdr=new Uint8Array(logical.subarray(0,0x40)); enc(hdr,0,0x40);
  const count=(hdr[8]|(hdr[9]<<8));
  const files={};
  for(let i=0;i<count;i++){
   const e=new Uint8Array(logical.subarray(0x40+i*0x50,0x40+(i+1)*0x50)); enc(e,0,0x50);
   const dv=new DataView(e.buffer);
   const typ=dv.getUint16(0,true);
   const off=dv.getUint32(12,true), len=dv.getUint32(16,true);
   let name=""; for(let j=0x40;j<0x50&&e[j];j++) name+=String.fromCharCode(e[j]);
   files[name]={typ,off,len};
  }
  return files;
 }

 function status(s){ $("#fl-status").textContent=s; }
 function fail(s){ status("error: "+s); $("#fl-install").disabled=false; }

 async function detect(){ return await find(null,3,true); }
 async function detectLoader(){ return await find(id=>id.loader,30,false); }
 async function detectApp(){ return await find(id=>!id.loader,40,false); }

 async function install(){
  $("#fl-install").disabled=true;
  status("loading the package…");
  const resp=await fetch("/pkg");
  if(resp.status!==200){ fail("no package — run tools/pa_pkg.sh first"); return; }
  const buf=new Uint8Array(await resp.arrayBuffer());
  const logical=unpackPackage(buf);
  const files=parseUfw(logical);
  if(!files["flash.bin"]||!files["ota.bin"]){ fail("package missing flash/ota"); return; }
  let image=logical.subarray(files["flash.bin"].off, files["flash.bin"].off+files["flash.bin"].len);

  status("detecting the Pocket AMP (Web MIDI)…");
  let dev=await detect();
  if(!dev){ fail("Pocket AMP not found (USB data cable? Chrome/Edge? another app using it?)"); return; }
  status("device: "+dev.text+" ("+dev.model+")");
  if(!dev.loader){
   if(!dev.model.startsWith("Pocket AMP")){ fail("wrong model "+dev.model); return; }
   status("starting the update on "+dev.text+"…");
   send(UPGRADE); await sleep(2000);
   const ck=await serve(image, FINISH_CHECK, 8000, null);
   if(!ck.ok){ fail("package check did not finish ("+ck.why+")"); return; }
   status("switching to the update loader…"); await sleep(3000);
  } else {
   status("device is in update mode — finishing the write");
  }
  dev=await detectLoader();
  if(!dev){ fail("the update loader did not appear (replug USB and run again)"); return; }
  status("writing the app area…");
  send(UPGRADE); await sleep(2000);
  const wr=await serve(image, FINISH_WRITE, 180000, (k,end)=>{
   status("writing "+(Math.min(99,Math.floor(end*100/image.length)))+"%");
  });
  if(!wr.ok){ fail("write did not finish ("+wr.why+") — replug and run again"); return; }
  status("restarting…"); await sleep(4000);
  const back=await detectApp();
  if(!back){ fail("the Pocket AMP did not come back — power-cycle it"); return; }
  status("done: the Pocket AMP now runs "+back.text);
  $("#fl-install").disabled=false;
 }

 (async()=>{
  try{
   const info=await(await fetch("/pkg-info")).json();
   status(info.exists? "package ready: "+info.name+" ("+info.size+" B)" : "no package — run tools/pa_pkg.sh first");
   $("#fl-install").disabled=!info.exists;
  }catch(e){ status("package: server unreachable"); }
 })();
 $("#fl-detect").onclick=async()=>{
  status("detecting…");
  const dev=await detect();
  status(dev? "device: "+dev.text+" ("+dev.model+")" : "not found (USB data cable? Chrome/Edge? another app using it?)");
 };
 $("#fl-install").onclick=install;
})();
</script></body></html>"""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=8321)
    ap.add_argument("--host", default="0.0.0.0",
                    help="bind address (default 0.0.0.0 so Windows can reach it)")
    a = ap.parse_args()
    srv = ThreadingHTTPServer((a.host, a.port), H)
    print(f"Pocket AMP emulator: http://localhost:{a.port}/")
    if a.host in ("0.0.0.0", "::"):
        try:
            import socket
            ip = socket.gethostbyname(socket.gethostname())
            print(f"  (if localhost does not reach it, try http://{ip}:{a.port}/)")
        except Exception:
            pass
    try:
        srv.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        emu.close()


if __name__ == "__main__":
    main()
