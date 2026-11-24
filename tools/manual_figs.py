#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 hacha
"""The manual's two numbered figures, drawn from the screenshots (app/docs/img, ./test.sh screens):

  img/panel.svg       the FM-1's panel (fm1-base's web/fm1panel.js, its layout), each control numbered (4. Controls)
  img/screen-map.svg  the screen (screen-rec.png, at twice its size), each part numbered (3. The screen)
  img/keys-throw.svg  the keys, each with its number and its sound or rhythm (6. The keys)
  img/keys-fx.svg     the keys on ECHO, each with its number and its effect or its part's control (7. The performance)

The keys' names are read from the code (app/dsp: KEY_SOUND, the rhythms', FX_NAME), so they follow it.

The numbers alone, no words: one figure for both manuals, the list under it says what each is.
  python3 tools/manual_figs.py"""
import base64
import io
import re
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
IMG = ROOT / "app" / "docs" / "img"
HOT, PAGE = "#e5482e", "#d9a21b"                      # the manual's vermilion (a number), its mustard (a page's button)
FONT = "'Helvetica Neue', Helvetica, Arial, sans-serif"


def png64(name):
    """a screenshot, packed small (the shots are stored plain), as a data URL"""
    buf = io.BytesIO()
    Image.open(IMG / name).convert("RGB").quantize(64).save(buf, "PNG", optimize=True)
    return "data:image/png;base64," + base64.b64encode(buf.getvalue()).decode()


def badge(x, y, n, r=16):
    return (f'<circle cx="{x}" cy="{y}" r="{r}" fill="{HOT}" stroke="#fff" stroke-width="2"/>'
            f'<text x="{x}" y="{y + r * 0.38:.1f}" text-anchor="middle" font-family="{FONT}" font-size="{r * 1.1:.0f}" '
            f'font-weight="700" fill="#fff">{n}</text>')


def lead(x0, y0, x1, y1):
    """a leader from a number to the thing: a line and a dot at the thing"""
    return (f'<line x1="{x0}" y1="{y0}" x2="{x1}" y2="{y1}" stroke="{HOT}" stroke-width="2"/>'
            f'<circle cx="{x1}" cy="{y1}" r="3.5" fill="{HOT}"/>')


def panel():
    BODY, TRAY, KEY_W, KEY_B, KNOB, BTNC, LABEL = "#3a3d42", "#26282c", "#d9dadc", "#c9cacd", "#1f2124", "#2e3135", "#e8e8ea"
    o = []

    def text(x, y, s, size, fill=LABEL):
        o.append(f'<text x="{x}" y="{y}" text-anchor="middle" font-family="{FONT}" font-size="{size}" font-weight="600" '
                 f'fill="{fill}" letter-spacing=".05em">{s}</text>')

    def knob(x, y, r, label):
        o.append(f'<circle cx="{x}" cy="{y}" r="{r + 5}" fill="{TRAY}"/>'
                 f'<circle cx="{x}" cy="{y}" r="{r}" fill="{KNOB}" stroke="#44474c" stroke-width="2"/>'
                 f'<line x1="{x}" y1="{y - r + 5}" x2="{x}" y2="{y - r + 15}" stroke="{LABEL}" stroke-width="3" '
                 f'stroke-linecap="round"/>')
        text(x, y - r - 12, label, 17)

    def button(x, y, w, h, label, page=False, two=None):
        fill, ink = (PAGE, "#2a1d00") if page else (BTNC, LABEL)
        o.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="9" fill="{fill}" stroke="#4a4d52" stroke-width="1.5"/>')
        if two:
            text(x + w / 2, y + h / 2 - 1, label, 12, ink)
            text(x + w / 2, y + h / 2 + 12, two, 12, ink)
        else:
            text(x + w / 2, y + h / 2 + 5, label, 15, ink)

    o.append(f'<rect x="4" y="4" width="992" height="632" rx="44" fill="{BODY}"/>')
    knob(105, 100, 24, "MASTER")
    knob(215, 100, 24, "SELECT")
    knob(105, 205, 24, "PRESETS")
    knob(215, 205, 24, "ALGORITHM")
    for i in range(4):
        knob(610 + i * 104, 100, 22, f"K{i + 1}")
    o.append(f'<rect x="58" y="258" width="210" height="58" rx="14" fill="{TRAY}"/>')
    button(72, 268, 82, 38, "OCT−")
    button(170, 268, 82, 38, "OCT+")
    o.append('<rect x="300" y="44" width="236" height="236" rx="26" fill="#111214"/>'
             f'<image x="306" y="50" width="224" height="224" href="{png64("screen-playing.png")}" '
             'style="image-rendering: pixelated"/>')
    o.append(f'<rect x="568" y="160" width="400" height="140" rx="18" fill="{TRAY}"/>')
    pages = {"FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SEQ"}
    for i, b in enumerate(["FX", "SEL", "ENV", "LFO", "EDIT", "GLO"]):
        button(582 + i * 63, 174, 54, 48, b, b in pages)
    for i, b in enumerate(["HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC"]):
        button(582 + i * 63, 236, 54, 48, b, b in pages, "STOP" if b == "PLAY" else None)
    x0, y0, w, h = 36, 340, 928, 270                  # the keys: 16 white, 11 black over them
    o.append(f'<rect x="{x0}" y="{y0}" width="{w}" height="{h}" rx="18" fill="{TRAY}"/>')
    pitch = (w - 24) / 16
    kw = pitch * 0.78
    for a in [0, 1, 2, 4, 5, 7, 8, 9, 11, 12, 14]:
        o.append(f'<rect x="{x0 + 12 + (a + 1) * pitch - kw / 2:.1f}" y="{y0 + h * 0.06:.1f}" width="{kw:.1f}" '
                 f'height="{h * 0.33:.1f}" rx="{kw / 2:.1f}" fill="{KEY_B}"/>')
    for i in range(16):
        o.append(f'<rect x="{x0 + 12 + i * pitch + (pitch - kw) / 2:.1f}" y="{y0 + h * 0.43:.1f}" width="{kw:.1f}" '
                 f'height="{h * 0.52:.1f}" rx="{kw / 2:.1f}" fill="{KEY_W}"/>')

    # the numbers, in the order of the manual's list
    o.append(badge(46, 100, 1))                        # MASTER
    o.append(badge(266, 100, 2))                       # SELECT
    o.append(badge(46, 205, 3))                        # PRESETS
    o.append(badge(266, 205, 4))                       # ALGORITHM
    o.append(badge(34, 287, 5))                        # OCT− OCT+
    o.append(badge(300, 44, 6))                        # the screen
    o.append(f'<line x1="588" y1="140" x2="944" y2="140" stroke="{HOT}" stroke-width="2"/>')
    o.append(badge(766, 140, 7))                       # K1-K4
    o.append(badge(552, 229, 8))                       # the pages' buttons
    for n, i in ((9, 1), (10, 2), (11, 4), (12, 5)):   # SAVE, ARP, PLAY, REC
        o.append(badge(582 + i * 63 + 27, 320, n))
    o.append(badge(20, 400, 13))                       # the black keys
    o.append(badge(20, 526, 14))                       # the white keys
    return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1000 640" width="1000" height="640">'
            + "".join(o) + "</svg>\n")


def screen_map():
    S, X, Y = 2, 80, 56                                # twice the screen's size, from (X, Y)
    W, H = X + 240 * S + 80, Y + 240 * S + 24

    def at(x, y):
        return X + x * S, Y + y * S

    o = [f'<rect width="{W}" height="{H}" rx="16" fill="#1a1b1e"/>',
         f'<image x="{X}" y="{Y}" width="{240 * S}" height="{240 * S}" href="{png64("screen-rec.png")}" '
         'style="image-rendering: pixelated"/>']
    L, R = 36, W - 36                                  # the numbers' columns, left and right of the screen
    for n, (x, y), (bx, by) in [
        (1, (9, 13), (L, at(0, 13)[1])),               # the world's name (and a message)
        (2, (204, 7), (at(204, 0)[0], 24)),            # the tempo
        (3, (230, 15), (R, at(0, 15)[1])),             # the beat's lamp
        (4, (8, 85), (L, at(0, 85)[1])),               # the lanes
        (5, (8, 151), (L, at(0, 151)[1])),             # the phrase
        (6, (228, 167), (R, at(0, 167)[1])),           # the sound thrown
        (7, (6, 187), (L, at(0, 187)[1])),             # the rule's left
        (8, (230, 186), (R, at(0, 196)[1])),           # the page, and the pages' row
        (9, (6, 216), (L, at(0, 226)[1])),             # the knobs
    ]:
        tx, ty = at(x, y)
        o.append(lead(bx, by, tx, ty))
        o.append(badge(bx, by, n))
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}">'
            + "".join(o) + "</svg>\n")


def names(path, pattern):
    """a C table's quoted names, in order"""
    return re.findall(r'"([^"]+)"', re.search(pattern, (ROOT / path).read_text(), re.S).group(1))


def keys(white, black, groups=None):
    """the keybed (the panel's), each key its number under it and its name along it; groups: a label under each four
    white keys (ECHO's rows)"""
    INK, NUM = "#1e2b5a", "#5d6585"
    x0, y0, w, h = 10, 10, 980, 330                 # (the black keys taller than the panel's, for their names)
    H = y0 + h + (44 if groups else 10)
    pitch = (w - 24) / 16
    kw = pitch * 0.78
    o = [f'<rect width="1000" height="{H}" rx="16" fill="#3a3d42"/>',
         f'<rect x="{x0}" y="{y0}" width="{w}" height="{h}" rx="18" fill="#26282c"/>']

    def key(x, y, kh, fill, name, n):
        cx = x + kw / 2
        o.append(f'<rect x="{x:.1f}" y="{y:.1f}" width="{kw:.1f}" height="{kh:.1f}" rx="{kw / 2:.1f}" fill="{fill}"/>')
        ty = y + (kh - 30) / 2 + 4             # the name, read from below, in the key above its number
        o.append(f'<text transform="translate({cx + 6:.1f} {ty:.1f}) rotate(-90)" text-anchor="middle" '
                 f'font-family="{FONT}" font-size="17" font-weight="700" fill="{INK}">{name}</text>')
        o.append(f'<text x="{cx:.1f}" y="{y + kh - 12:.1f}" text-anchor="middle" font-family="{FONT}" font-size="16" '
                 f'font-weight="700" fill="{NUM}">{n}</text>')

    for k, a in enumerate([0, 1, 2, 4, 5, 7, 8, 9, 11, 12, 14]):
        key(x0 + 12 + (a + 1) * pitch - kw / 2, y0 + h * 0.05, h * 0.40, "#c9cacd", black[k], k + 1)
    tint = ["#f6d0c4", "#f3e2b0", "#cfe3e0", "#dcd3ea"]
    for i in range(16):
        key(x0 + 12 + i * pitch + (pitch - kw) / 2, y0 + h * 0.49, h * 0.47, tint[i // 4] if groups else "#d9dadc",
            white[i], i + 1)
    if groups:                                         # a bracket and a name under each row of four
        for g, label in enumerate(groups):
            a, b = x0 + 12 + g * 4 * pitch + 6, x0 + 12 + (g + 1) * 4 * pitch - 6
            o.append(f'<path d="M{a:.1f} {y0 + h + 6} v8 H{b:.1f} v-8" fill="none" stroke="{tint[g]}" stroke-width="2"/>')
            o.append(f'<text x="{(a + b) / 2:.1f}" y="{y0 + h + 34}" text-anchor="middle" font-family="{FONT}" '
                     f'font-size="17" font-weight="700" fill="{tint[g]}" letter-spacing=".08em">{label}</text>')
    return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 1000 {H}" width="1000" height="{H}">'
            + "".join(o) + "</svg>\n")


if __name__ == "__main__":
    sounds = names("app/dsp/events.c", r"KEY_SOUND\[16\] = \{(.*?)\n\};")
    sounds = [n for n in sounds]
    rhythms = names("app/dsp/psy.c", r"ps_rhythm_name.*?N\[11\] = \{(.*?)\};")
    fx = names("app/dsp/fx.c", r"FX_NAME\[FX_N\] = \{(.*?)\};")
    assert len(sounds) == 16 and len(rhythms) == 11 and len(fx) == 16, (sounds, rhythms, fx)
    (IMG / "keys-throw.svg").write_text(keys(sounds, rhythms))
    (IMG / "keys-fx.svg").write_text(keys(fx, ["KICK", "BASS", "PERC", "EVENTS", "LINE", "ATMOS", "FILL", "HALF", "GAP",
                                               "K+B OUT", "ALL"], ["REPEAT", "FILTER", "SPACE", "MOTION"]))
    (IMG / "panel.svg").write_text(panel())
    (IMG / "screen-map.svg").write_text(screen_map())
    print("panel.svg, screen-map.svg, keys-throw.svg, keys-fx.svg in", IMG)
