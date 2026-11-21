#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 hacha
# The boot's face (app/face.h): a rhino-tiger, front on, wide-eyed, its fur in nested spiked layers and the air round
# it in rings, all of it in bands of colour that the app turns through (each pixel a band and a level). Drawn here
# pixel by pixel (three samples a side, the levels averaged, the band the middle one's), left-right mirrored.
# Bands 0..13 turn (the app adds the time to their hue), 14 is white (the whiskers, the eyes' glints, the fangs),
# 15 gold (the eyes); level 0 is black.
# Usage: tools/face.py [preview.png]   (writes app/face.h; with a path, a look at it too: four moments side by side)
import os, sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
SS = 3                                   # samples a side
N = 240 * SS
TURN = 14                                # the bands that turn
WHITE, GOLD = 14, 15

c = (np.arange(N) + 0.5) / SS - 120.0
X, Y = np.meshgrid(c, c)                 # (x right, y down; the middle 0)
AX = np.abs(X)                           # (mirrored)
band = np.zeros((N, N), np.int16)
level = np.zeros((N, N), np.float32)


def tri(u):                              # a triangle wave, 0..1..0 over each 1
    u = u - np.floor(u)
    return 1.0 - np.abs(2.0 * u - 1.0)


def put(mask, b, lv):
    band[mask] = (b[mask] if isinstance(b, np.ndarray) else b)
    level[mask] = (lv[mask] if isinstance(lv, np.ndarray) else lv)


# the air: rings out from behind the head, scalloped like petals, each a band (the colours flow outward), a dark seam between
CY = 14.0
r0 = np.hypot(X, Y - CY)
th0 = np.arctan2(AX, -(Y - CY))          # 0 up, pi down
rr = r0 - 8.0 * np.abs(np.sin(th0 * 7 + np.floor(r0 / 13.0) * 0.45)) ** 0.7
k = np.floor(rr / 13.0)
f = rr / 13.0 - k
lv = 3.5 + 4.0 * f
lv = np.where(f < 0.16, 0.0, lv)
dots = (k % 2 == 1) & (np.hypot((tri(th0 * (6 + 3 * k) / np.pi) - 0.5) * 2 * r0 * np.pi / (6 + 3 * k + 1e-6) / 2.2, (f - 0.58) * 13.0) < 2.6)
lv = np.where(dots, 11.0, lv)
put(np.ones_like(X, bool), (k.astype(np.int16) * 1) % TURN, lv)

# the head: a wide shield, its ruff in spikes below and at the sides; ears up and out
HC = 10.0
rh = np.hypot(X * 0.88, Y - HC)
thh = np.arctan2(AX, -(Y - HC))
spikes = 13.0 * tri(thh * 22 / np.pi) ** 1.6 * np.clip((thh - 0.9) / 0.6, 0.0, 1.0)
R = 84.0 + spikes + 10.0 * np.clip((thh - 1.2), 0, 1)
s = rh / R                               # 0 the middle .. 1 the edge
head = s < 1.0
# the ears: nested triangles, tall, leaning out (from their middle, scaled: each step in a band)
def ear_s(px, py):
    # the ear's triangle (its right one; mirrored), in barycentric terms: s the largest scale it is inside of
    A = np.array([44.0, -58.0]); B = np.array([106.0, -116.0]); C = np.array([104.0, -30.0])
    G = (A + B + C) / 3
    best = np.full(px.shape, 9.0)
    for P0, P1 in ((A, B), (B, C), (C, A)):
        n = np.array([P1[1] - P0[1], -(P1[0] - P0[0])])
        n = n / np.hypot(*n)
        if np.dot(G - P0, n) > 0:
            n = -n
        d0 = np.dot(P0 - G, n)
        best = np.minimum(best, np.where(True, 1.0 / np.maximum((((px - G[0]) * n[0] + (py - G[1]) * n[1]) / d0), 1e-6), 0))
    return 1.0 / best                    # the triangle's scale the point lies on (1: its edge)
es = ear_s(AX, Y)
es = es + 0.07 * tri(np.arctan2(Y + 68.0, AX - 84.7) * 14 / np.pi) * np.clip(es, 0, 1)   # (furred: its layers spiked)
ear = es < 1.0
ke = np.floor(es * 5.0)
put(ear, (9 + ke.astype(np.int16)) % TURN, np.where((es * 5.0 - ke) < 0.18, 1.0, 12.0 - 1.2 * ke))
put(ear & (es < 0.3), np.int16(3), np.full_like(X, 2.0))      # the ear's dark hollow

# the fur: the head's own rings, inward (scaled copies of its edge, spiked), each a band, a dark line between
sp = s + 0.045 * tri(thh * 34 / np.pi + s * 3.0)
kf = np.floor(sp * 9.0)
ff = sp * 9.0 - kf
lv = np.where(ff < 0.14, 1.0, 9.5 + 3.0 * ff)
put(head, (20 - kf.astype(np.int16)) % TURN, lv)

# the tiger's stripes: wedges in from the head's edge, tapering, a little curved, dark
for a0, w, depth in ((0.30, 0.10, 0.50), (0.62, 0.08, 0.62), (1.42, 0.09, 0.50), (1.70, 0.10, 0.42), (1.98, 0.09, 0.45),
                     (2.30, 0.08, 0.55)):
    d = thh - a0 - 0.35 * (1.0 - s) ** 2
    stripe = head & (s > depth) & (np.abs(d) < w * (s - depth) / (1.0 - depth))
    put(stripe, band, np.full_like(X, 1.0))

# the eyes: great and round, ringed out in bands, gold irises lined like rays, slit pupils, a glint
EX, EY, ER = 81.0, -30.0, 23.0
ex, ey = AX - EX, Y - EY
re = np.hypot(ex, ey)
ring = (re < ER + 16) & (re >= ER)
kr = np.floor((re - ER) / 5.3)
put(ring & head, (kr.astype(np.int16) + 2) % TURN, np.where((re - ER) - kr * 5.3 < 1.0, 0.0, 13.0))
put(re < ER, np.int16(GOLD), 9.0 + 4.0 * (0.5 + 0.5 * np.cos(np.arctan2(ey, ex) * 18)) * np.clip(re / ER, 0.3, 1) + 2 * (re < ER * 0.55))
put((re < ER) & (re > ER - 2.2), np.int16(GOLD), np.full_like(X, 4.0))
pupil = (np.abs(ex) < 6.5 * np.sqrt(np.clip(1 - (ey / (ER * 0.9)) ** 2, 0, 1))) & (re < ER)
put(pupil, np.int16(GOLD), np.full_like(X, 0.0))
glint = np.hypot(ex + 9, ey + 9) < 4.2
put(glint, np.int16(WHITE), np.full_like(X, 15.0))

# the horn: from the nose up, ringed along its length
hy0, hy1 = 34.0, -112.0
hw = 15.0 * np.clip((Y - hy1) / (hy0 - hy1), 0, 1) ** 1.3
horn = (Y < hy0) & (Y > hy1) & (AX < hw)
kh = np.floor((Y - hy1) / 11.0)
put(horn, (kh.astype(np.int16) + 5) % TURN, np.where(((Y - hy1) / 11.0 - kh) < 0.2, 3.0, 14.0 - 6.0 * AX / np.maximum(hw, 1)))

# the muzzle: two round pads, the nose, the mouth, fangs
rp = np.hypot(AX - 15, (Y - 54) * 1.15)
pad = (rp < 17) & ~horn
put(pad, band, np.where(rp > 15.5, 1.0, np.where(np.hypot((AX - 15) % 6 - 3, (Y - 54) % 6 - 3) < 1.1, 4.0, 14.0)))
nose = (Y > 30) & (Y < 42) & (AX < 13 - (Y - 30) * 0.9)
put(nose, np.int16(0), np.full_like(X, 2.0))
my = 70 + 5.0 * tri(AX / 30.0 + 0.5) - 5.0
mouth = (np.abs(Y - my) < 1.3) & (AX < 30)
put(mouth, np.int16(0), np.full_like(X, 0.0))
fang = (Y >= my) & (Y < 69 + 14 * (1 - np.abs(AX - 14) / 4.0)) & (np.abs(AX - 14) < 4.0)
put(fang, np.int16(WHITE), np.full_like(X, 14.0))

# the whiskers: long and fine, from the pads out past the head, a little curved
for i, (y0, a, bend) in enumerate(((46, -0.28, 0.0016), (54, -0.05, 0.0012), (62, 0.16, 0.0008))):
    u = AX - 30
    wy = y0 + a * u + bend * u * u
    m = (u > 0) & (np.abs(Y - wy) < 0.75 + 0.004 * (120 - u))
    put(m, np.int16(WHITE), np.full_like(X, 13.0))

# the samples to pixels: the levels averaged, the band the middle one's
lvl = level.reshape(240, SS, 240, SS).mean(axis=(1, 3))
bnd = band[SS // 2::SS, SS // 2::SS]
lvl = np.clip(np.rint(lvl), 0, 15).astype(np.uint8)
bnd = bnd.astype(np.uint8)
face = (bnd << 4) | lvl
face[lvl == 0] = 0

lines = ['/* SPDX-License-Identifier: GPL-3.0-only',
         ' * Copyright (C) 2026 hacha */',
         "/* The boot's face: a rhino-tiger, 240 x 240, row by row, a byte a pixel: its band (high four bits: 0..13 turn",
         ' * through the hues, 14 white, 15 gold) and its level (low four, 0 black .. 15 the brightest). Made by',
         ' * tools/face.py; not to be edited by hand. */',
         '#define FACE_W 240',
         '#define FACE_H 240',
         '#define FACE_TURN %d' % TURN,
         'static const uint8_t FACE[FACE_W * FACE_H] = {']
flat = face.flatten()
for i in range(0, len(flat), 24):
    lines.append('    ' + ', '.join('0x%02X' % b for b in flat[i:i + 24]) + ',')
lines.append('};')
open(os.path.join(HERE, '..', 'app', 'face.h'), 'w').write('\n'.join(lines) + '\n')
print('app/face.h:', len(flat), 'bytes')

if len(sys.argv) > 1:                    # a look: four moments, as the app colours them (roughly)
    import colorsys
    from PIL import Image
    shots = []
    for ph in range(4):
        img = np.zeros((240, 240, 3), np.uint8)
        for b in range(16):
            for l in range(1, 16):
                m = (bnd == b) & (lvl == l)
                if not m.any():
                    continue
                if b == GOLD:
                    rgb = colorsys.hsv_to_rgb(0.11, 0.75, l / 15)
                elif b == WHITE:
                    rgb = (l / 15,) * 3
                else:
                    rgb = colorsys.hsv_to_rgb(((b - ph * 1.5) / TURN) % 1.0, 0.88, l / 15)
                img[m] = [int(v * 255) for v in rgb]
        shots.append(img)
    import re
    src = open(os.path.join(HERE, '..', 'app', 'logo.h')).read()
    px = int(re.search(r'#define LOGO_PX (\d+)', src).group(1))
    vals = [int(v, 16) for v in re.findall(r'0x([0-9A-F]{2})', src.split('LOGO_RLE[LOGO_RLE_N] = {')[1])]
    cov = []
    for i in range(0, len(vals), 2):
        cov += [vals[i]] * vals[i + 1]
    cov = np.array(cov[:2 * px * px]).reshape(2 * px, px)
    lx, ly = 120 - px // 2, (240 - 2 * px - 4) // 2
    for img in shots[:]:
        o = img.astype(np.float32).copy()
        for ch in range(2):
            cv = cov[ch * px:(ch + 1) * px]
            y0 = ly + ch * (px + 4)
            for row in range(px):
                hue = (1365 - (ch * px + row) * (1365 - 405) / (2 * px)) / 1536
                col = np.array(colorsys.hsv_to_rgb(hue % 1, 0.9, 1.0)) * 255
                ring = (cv[row] & 15)[:, None] / 15.0
                ink = (cv[row] >> 4)[:, None] / 15.0
                seg = o[y0 + row, lx:lx + px]
                seg = seg * (1 - ring)
                o[y0 + row, lx:lx + px] = seg * (1 - ink) + col * ink
        o[106:134] = 0
        for x in range(0, 240, 4):
            col = np.array(colorsys.hsv_to_rgb(((1365 - x * 4) / 1536) % 1, 0.9, 1.0)) * 255
            o[106:108, x:x + 4] = col; o[132:134, x:x + 4] = col
        shots.append(o.astype(np.uint8))
    shots = [shots[0], shots[2], shots[4], shots[6]]
    out = np.concatenate(shots, axis=1)
    Image.fromarray(out).resize((out.shape[1] * 2, 480), Image.NEAREST).save(sys.argv[1])
