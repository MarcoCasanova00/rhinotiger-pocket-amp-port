#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 hacha
# The boot's face (app/face.h): a rhino-tiger, front on, angular as a mask (a rhino's head, ears, horns and snout, its
# eyes turned to the sides; a tiger's stripes, gold slit eyes and fangs), drawn as an SVG here (its left half,
# mirrored), rendered with rsvg-convert at 240 x 240, its greys taken to 16 levels and run-length coded. The app tints the levels.
# Usage: tools/face.py            (writes app/face.h; tools/face.svg and face.png beside it for a look)
import os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
def g(v): v=int(v*255); return f'#{v:02x}{v:02x}{v:02x}'
def mir(p): return [(240-x,y) for x,y in p]
def poly(p): return 'M'+' L'.join(f'{x},{y}' for x,y in p)+' Z'
out=[]
def P(pts, **a):          # a shape, and its mirror
    at=' '.join(f'{k.replace("_","-")}="{v}"' for k,v in a.items())
    out.append(f'<path d="{poly(pts)}" {at}/>'); out.append(f'<path d="{poly(mir(pts))}" {at}/>')
def W(pts, **a):          # a shape across the middle (given whole)
    at=' '.join(f'{k.replace("_","-")}="{v}"' for k,v in a.items())
    out.append(f'<path d="{poly(pts)}" {at}/>')
DARK=g(.04)
import math
# the ears: a rhino's, tall tubes cupped forward, tilted out, a fringe at the tips (drawn first: the head over their roots)
P([(82,54),(64,30),(42,8),(22,0),(26,14),(30,36),(50,60)], fill=g(.42), stroke=g(.72), stroke_width="2", stroke_linejoin="miter")
P([(72,50),(58,32),(40,14),(30,8),(34,30),(50,52)], fill=g(.12))
P([(22,0),(14,4),(26,8)], fill=g(.6)); P([(24,2),(20,-4),(30,6)], fill=g(.6)); P([(28,12),(18,16),(28,18)], fill=g(.6))
# the head: a shield, no ruff, the jaw drawn in
L=[(120,34),(90,36),(66,46),(40,64),(24,90),(20,122),(26,150),(24,172),(34,192),(60,212),(88,228),(120,232)]
W(L+mir(L)[::-1][1:-1], fill=g(.36), stroke=g(.72), stroke_width="2", stroke_linejoin="miter")
for y in (48,58): P([(70,y+8),(92,y),(112,y+2),(92,y+4)], fill=g(.22))          # the skin's folds
P([(54,56),(84,64),(58,66)], fill=DARK); P([(36,68),(70,74),(40,78)], fill=DARK)   # a tiger's stripes
P([(26,104),(84,112),(88,152),(62,172),(30,164),(22,130)], fill=g(.54))          # the cheek
P([(20,124),(64,126),(24,132)], fill=DARK); P([(22,146),(70,144),(26,154)], fill=DARK); P([(30,168),(66,160),(36,176)], fill=DARK)
# the brow, a scowl; the eye on the head's side: it and its slit turned outward 55 degrees round its middle, in perspective
P([(18,74),(49,78),(88,92),(86,98),(49,86),(20,82)], fill=g(.74))
EX, EW, EY, EH, TURN, F = 22, 54, 90, 20, math.radians(55), 110
XC, YC = EX + EW * .5, EY + EH * .3
def turn(p):
    d = p[0] - XC; s = F / (F - d * math.sin(TURN))
    return (round(XC + d * math.cos(TURN) * s / math.cos(TURN) ** .5, 2), round(YC + (p[1] - YC) * s, 2))
P([turn(q) for q in [(EX,EY),(EX+EW*.48,EY-EH*.2),(EX+EW,EY+EH*.55),(EX+EW*.48,EY+EH*.75)]], fill=g(.95), stroke=DARK, stroke_width="2", stroke_linejoin="miter")
P([turn(q) for q in [(XC,EY-EH*.12),(XC+4.3,EY+EH*.3),(XC,EY+EH*.7),(XC-4.3,EY+EH*.3)]], fill=DARK)
# the snout, its broad upper lip, the mouth and a tiger's fangs; the nostrils under the horn
M=[(120,104),(100,108),(88,150),(78,198),(98,214),(120,216)]
W(M+mir(M)[::-1][1:-1], fill=g(.3), stroke=g(.6), stroke_width="1.5")
U=[(120,184),(96,184),(82,196),(94,208),(120,210)]
W(U+mir(U)[::-1][1:-1], fill=g(.46))
P([(120,206),(98,206),(80,198),(94,212),(120,214)], fill=DARK)
P([(88,209),(97,209),(92,228)], fill=g(.86))
P([(92,166),(106,170),(104,182),(92,178)], fill=DARK)
# the horns: the small one behind, the great one from the nose (the eyes alone are brighter: they glow gold)
W([(100,96),(120,44),(140,96)], fill=g(.6), stroke=g(.8), stroke_width="1.5")
W([(90,162),(102,122),(112.8,78),(120,8),(127.2,78),(138,122),(150,162),(120,152)], fill=g(.8), stroke=g(.88), stroke_width="1.5")
W([(113,148),(117,88),(120,16),(119.5,88),(117.5,150)], fill=g(.88))
svg = '<svg xmlns="http://www.w3.org/2000/svg" width="240" height="240" viewBox="0 0 240 240"><rect width="240" height="240" fill="#000"/>' + ''.join(out) + '</svg>'
svg_path = os.path.join(HERE, 'face.svg')
png_path = os.path.join(HERE, 'face.png')
open(svg_path, 'w').write(svg)
subprocess.run(['rsvg-convert', '-w', '240', '-h', '240', svg_path, '-o', png_path], check=True)
from PIL import Image
im = Image.open(png_path).convert('L')
w, h = im.size
q = [min(15, (im.getpixel((x, y)) * 15 + 127) // 255) for y in range(h) for x in range(w)]
rle = []
i = 0
while i < len(q):
    r = 1
    while i + r < len(q) and q[i + r] == q[i] and r < 16:
        r += 1
    rle.append(q[i] << 4 | (r - 1))
    i += r
lines = ['/* SPDX-License-Identifier: GPL-3.0-only',
         ' * Copyright (C) 2026 hacha */',
         "/* The boot's face: a rhino-tiger, front on, 240 x 240, 16 levels (0 black .. 15 the brightest), row by row, run-",
         ' * length coded: each byte a level (its high four bits) and a run of 1..16 (its low four, less one). Made by',
         ' * tools/face.py (an SVG drawn there); not to be edited by hand. */',
         '#define FACE_W 240',
         '#define FACE_H 240',
         '#define FACE_N %d' % len(rle),
         'static const uint8_t FACE[FACE_N] = {']
for k in range(0, len(rle), 24):
    lines.append('    ' + ', '.join('0x%02X' % b for b in rle[k:k + 24]) + ',')
lines.append('};')
open(os.path.join(HERE, '..', 'app', 'face.h'), 'w').write('\n'.join(lines) + '\n')
print('app/face.h:', len(rle), 'bytes')
