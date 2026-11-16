#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 hacha
# The boot's face (app/face.h): a rhino-tiger, front on, angular as a mask (a scowl, slit eyes, the rhino's horns and
# armour, a tiger's blades of fur and fangs), drawn as an SVG here (its left half, mirrored), rendered with
# rsvg-convert at 240 x 240, its greys taken to 16 levels and run-length coded. The app tints the levels.
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
if False:  # (rays behind the head: hidden by it, not drawn)
    import math
    for k in range(36):
        a0=math.radians(k*10+2); a1=math.radians(k*10+7)
        r0,r1=60,128 if k%2 else 112
        pts=[(120+r0*math.cos(a0),118+r0*math.sin(a0)),(120+r1*math.cos((a0+a1)/2),118+r1*math.sin((a0+a1)/2)),(120+r0*math.cos(a1),118+r0*math.sin(a1))]
        W([(round(x,1),round(y,1)) for x,y in pts], fill=g(.22))
# the head: ears swept out, a ruff of blades, a heavy jaw
L=[(120,30),(90,30),(66,34),(26,2),(30,34),(22,58),(4,80),(20,90),(0,114),(18,122),(4,148),(26,150),(18,176),(46,174),(46,200),(72,206),(90,224),(120,236)]
W(L+mir(L)[::-1][1:-1], fill=g(.36), stroke=g(.72), stroke_width="2", stroke_linejoin="miter")
P([(32,12),(36,34),(60,36)], fill=g(.16))                      # the ear's inside
P([(34,20),(38,32),(48,33)], fill=g(.6))                       # its tuft
# the forehead: blades of dark fur, converging down to the middle
P([(116,34),(92,32),(110,44)], fill=DARK)
P([(114,50),(84,40),(106,58)], fill=DARK)
P([(112,64),(78,52),(102,70)], fill=DARK)
P([(64,40),(40,40),(62,50)], fill=DARK)
P([(26,60),(58,58),(30,68)], fill=DARK)
# the brow: a heavy plate, low to the middle (a scowl)
P([(32,66),(70,72),(108,90),(110,98),(68,84),(34,78)], fill=g(.78))
# the eye: narrow, its inner end lower, a slit; a dark line from it down beside the nose
P([(40,84),(70,84),(102,98),(70,104)], fill=g(.95), stroke=DARK, stroke_width="2", stroke_linejoin="miter")
P([(72.5,84),(76,94),(72.5,104),(69,94)], fill=DARK)
P([(102,98),(108,104),(100,140),(98,108)], fill=DARK)
# the cheeks: pale fur in blades, stripes cutting in from the edge
P([(28,98),(86,110),(92,150),(60,172),(30,160),(18,128)], fill=g(.56))
P([(4,112),(54,116),(10,122)], fill=DARK)
P([(8,140),(62,138),(14,148)], fill=DARK)
P([(24,166),(64,156),(32,174)], fill=DARK)
# the muzzle: a rhino's armour, plates across it
W([(100,104),(140,104),(156,150),(150,186),(120,196),(90,186),(84,150)], fill=g(.3), stroke=g(.6), stroke_width="1.5")
for y in (128,146,164):
    P([(88,y+6),(120,y),(120,y+4),(90,y+10)], fill=g(.55))
# the great horn from the nose, broad at its root; the small one behind, above
W([(110,98),(120,62),(130,98)], fill=g(.7), stroke=g(.86), stroke_width="1")
W([(98,180),(110,130),(120,92),(130,130),(142,180),(120,174)], fill=g(.8), stroke=g(.88), stroke_width="1.5")
W([(112,170),(118,112),(121,96),(120,120),(117,172)], fill=g(.88))
# the nose, the mouth, the fangs
W([(104,178),(136,178),(120,192)], fill=DARK)
W([(119,192),(121,192),(121,202),(119,202)], fill=DARK)
P([(120,202),(98,206),(84,200),(98,210),(120,208)], fill=DARK)
P([(98,206),(106,205),(102,224)], fill=g(.86))
# whiskers
P([(84,168),(10,156),(10,158),(84,171)], fill=g(.6))
P([(84,176),(8,180),(8,182),(84,179)], fill=g(.6))
P([(86,184),(18,202),(19,204),(87,187)], fill=g(.6))
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
