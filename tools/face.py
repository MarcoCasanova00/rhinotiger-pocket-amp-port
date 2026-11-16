#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 hacha
# The boot's face (app/face.h): a rhino-tiger, front on, drawn as an SVG here (its left half, mirrored), rendered with
# rsvg-convert at 240 x 240, its greys taken to 16 levels and run-length coded. The app tints the levels.
# Usage: tools/face.py            (writes app/face.h; tools/face.svg and face.png beside it for a look)
import os, subprocess, sys
HERE = os.path.dirname(os.path.abspath(__file__))
def g(v): v=int(v*255); return f'#{v:02x}{v:02x}{v:02x}'
def mir(pts): return [(240-x,y) for x,y in pts]
def poly(pts): return 'M'+' L'.join(f'{x},{y}' for x,y in pts)+' Z'
def cpath(segs):   # segs: start + list of cubic (c1,c2,p)
    s=f'M{segs[0][0]},{segs[0][1]}'
    for c1,c2,p in segs[1:]: s+=f' C{c1[0]},{c1[1]} {c2[0]},{c2[1]} {p[0]},{p[1]}'
    return s+' Z'
def mirc(segs): return [(240-segs[0][0],segs[0][1])]+[tuple((240-x,y) for x,y in t) for t in segs[1:]]
out=[]
def both(kind, data, dy=0, **a):
    attr=' '.join(f'{k.replace("_","-")}="{v}"' for k,v in a.items())
    if dy:
        data=[(x,y+dy) for x,y in data] if kind=='p' else [(data[0][0],data[0][1]+dy)]+[tuple((x,y+dy) for x,y in t) for t in data[1:]]
    for d in (data, (mir(data) if kind=='p' else mirc(data))):
        out.append(f'<path d="{poly(d) if kind=="p" else cpath(d)}" {attr}/>')
EYE=-20                                  # the eyes and the brow raised (the band over the middle: they stay above it)
FUR=g(.42); FUR2=g(.55); DARK=g(.06); LINE=g(.7)
# the head and its ruff, whole
L=[(120,34),(92,36),(72,40),(54,8),(40,22),(32,48),(24,70),(20,88),(4,100),(18,112),(2,130),(20,138),(6,158),(26,164),(16,186),(40,186),(36,208),(64,206),(80,222),(104,226),(120,232)]
head=L+mir(L)[::-1][1:-1]
out.append(f'<path d="{poly(head)}" fill="{FUR}" stroke="{LINE}" stroke-width="2"/>')
# the ears' insides, tufts
both('p',[(54,16),(42,46),(66,42)],fill=g(.25))
both('p',[(50,26),(46,40),(56,38)],fill=g(.75))
# the white over the eyes, the cheeks' pale fur
both('c',[(48,82),((58,66),(80,64),(96,76)),((84,80),(62,82),(48,82))],dy=EYE,fill=FUR2)
both('c',[(30,112),((48,110),(70,118),(84,140)),((76,170),(52,184),(30,180)),((20,160),(20,130),(30,112))],fill=FUR2)
# the stripes: the forehead's, stacked, narrowing to the middle; the temples'; the cheeks', sweeping in
both('c',[(120,40),((112,46),(102,50),(92,48)),((102,56),(112,56),(120,52))],fill=DARK)
both('c',[(118,58),((108,64),(98,66),(88,62)),((98,72),(110,72),(118,66))],fill=DARK)
both('c',[(114,74),((106,80),(100,80),(94,78)),((100,86),(108,86),(114,80))],dy=-6,fill=DARK)
both('c',[(60,44),((68,54),(78,58),(86,58)),((76,62),(64,58),(56,50))],fill=DARK)
both('c',[(34,62),((48,66),(60,72),(66,76)),((54,76),(40,72),(30,70))],dy=-14,fill=DARK)
both('c',[(8,104),((30,104),(46,110),(58,118)),((44,118),(26,114),(12,112))],fill=DARK)
both('c',[(6,134),((28,130),(44,134),(62,142)),((46,144),(26,142),(10,140))],fill=DARK)
both('c',[(10,160),((30,154),(46,156),(62,164)),((46,166),(28,166),(14,166))],fill=DARK)
both('c',[(38,188),((52,180),(62,178),(72,182)),((60,188),(50,190),(40,194))],fill=DARK)
# the brow's armour: a plate, ridged
both('c',[(42,92),((56,74),(86,72),(106,86)),((88,82),(62,84),(44,100))],dy=EYE,fill=g(.8))
# the eyes: slanted almonds, a slit
both('c',[(50,98),((62,86),(88,90),(102,106)),((88,116),(64,114),(50,98))],dy=EYE,fill=g(.95),stroke=DARK,stroke_width="2")
out.append(f'<ellipse cx="78" cy="{102+EYE}" rx="3.4" ry="9.5" fill="{DARK}"/><ellipse cx="162" cy="{102+EYE}" rx="3.4" ry="9.5" fill="{DARK}"/>')
# the muzzle: a rhino's, broad and folded, nostrils
both('c',[(120,112),((98,112),(80,134),(80,160)),((80,188),(98,206),(120,208))],fill=g(.3),stroke=LINE,stroke_width="1.5")
for y in (136,152,168):
    both('c',[(84,y+4),((96,y-4),(110,y-5),(120,y-4)),((110,y-2),(96,y-1),(84,y+6))],fill=g(.55))
out.append(f'<path d="M120,198 C114,203 106,205 98,203 M120,198 C126,203 134,205 142,203" fill="none" stroke="{LINE}" stroke-width="2"/>')
both('c',[(100,180),((96,174),(108,172),(112,182)),((108,188),(100,186),(100,180))],fill=DARK)
# whiskers
for a,b in (((84,172),(14,164)),((84,180),(12,184)),((88,188),(22,204))):
    both('p',[a,b,(b[0],b[1]+1.5),(a[0],a[1]+1.5)],fill=g(.6))
# the brow's horns, small
both('c',[(84,62),((88,48),(92,36),(98,26)),((98,40),(100,52),(100,66))],dy=-8,fill=g(.8),stroke=g(.88),stroke_width="1")
# the great horn on the nose, broad at its root, a ridge up it
out.append(f'<path d="M98,180 C104,140 112,96 121,46 C126,96 134,140 142,180 C128,172 112,172 98,180 Z" fill="{g(.78)}" stroke="{g(.88)}" stroke-width="1.5"/>')
out.append(f'<path d="M110,170 C114,130 118,96 121,56 L123,66 C121,100 118,132 116,170 Z" fill="{g(.88)}"/>')
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
