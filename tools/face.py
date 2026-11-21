#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 hacha
# The boot's face (app/face.h): a rhino-tiger, front on (a rhino's ears, horns, hide and snout; a tiger's stripes,
# white ruff and brows, lined eyes and fangs), its fur in nested spiked layers and the air round
# it in rings, all of it in bands of colour that the app turns through (each pixel a band and a level). Drawn here
# pixel by pixel (three samples a side, the levels averaged, the band the middle one's), left-right mirrored.
# Bands 0..13 turn (the app adds the time to their hue), 14 is white (the horns, the ruff, the brows, the fangs),
# 15 gold (the eyes); level 0 is black.
# Usage: tools/face.py [preview.png]   (writes app/face.h; with a path, a look at it too: four moments side by side)
import os, sys
import numpy as np

HERE = os.path.dirname(os.path.abspath(__file__))
SS = 3                                   # samples a side
N = 240 * SS
TURN = 14                                # the bands that turn
WHITE, GOLD = 14, 15                     # (white: the horns, the fangs, the eyes' glints)

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
# the ears: a rhino's, tubes cupped up and leaning out, nested rings, a fringe at the tip, a dark hollow
T = np.radians(34)
dx, dy = AX - 86.0, Y + 84.0
u = dx * np.sin(T) - dy * np.cos(T)          # (along the ear, up and out)
v = dx * np.cos(T) + dy * np.sin(T)          # (across it)
es = np.hypot(u / 36.0, v / (18.0 * (1.0 - 0.28 * np.clip(u / 36.0, -1, 1))))
es = es - 0.16 * tri(v / 5.0) * np.clip((u - 22.0) / 14.0, 0, 1)   # (its tip fringed)
ear = es < 1.0
ke = np.floor(es * 4.0)
put(ear, (9 + ke.astype(np.int16)) % TURN, np.where((es * 4.0 - ke) < 0.18, 1.0, 13.0 - 1.4 * ke))
put(ear & (np.hypot((u - 6.0) / 22.0, v / 8.0) < 1.0), np.int16(3), np.full_like(X, 2.0))

# the fur: the head's own rings, inward (scaled copies of its edge, spiked), each a band, a dark line between
sp = s + 0.045 * tri(thh * 34 / np.pi + s * 3.0)
kf = np.floor(sp * 9.0)
ff = sp * 9.0 - kf
lv = np.where(ff < 0.14, 1.0, 9.5 + 3.0 * ff)
put(head, (20 - kf.astype(np.int16)) % TURN, lv)

# the tiger's ruff: white, furred out past the head at the cheeks, in spiked locks
lock = tri(thh * 26 / np.pi)
ruff = (thh > 1.45) & (thh < 2.45) & (s > 0.58) & (s < 1.0 + 0.16 * lock ** 1.5 * np.clip((thh - 1.45) / 0.15, 0, 1)
                                                       * np.clip((2.45 - thh) / 0.15, 0, 1))
put(ruff, np.int16(WHITE), 10.5 + 3.5 * lock - 2.5 * np.clip((s - 0.9) / 0.2, 0, 1))
put(ruff & (np.abs(s - 0.58) < 0.012), np.int16(WHITE), np.full_like(X, 1.0))

# the tiger's stripes: black wedges in from the head's edge, tapering, a little curved, through the ruff too
for a0, w, depth in ((0.24, 0.065, 0.50), (0.54, 0.055, 0.60), (1.38, 0.065, 0.50), (1.72, 0.075, 0.44), (2.06, 0.07, 0.48),
                     (2.38, 0.06, 0.55)):
    d = thh - a0 - 0.35 * (1.0 - s) ** 2
    stripe = (head | ruff) & (s > depth) & (np.abs(d) < w * np.clip(s - depth, 0, 1) / (1.0 - depth))
    put(stripe, band, np.full_like(X, 0.0))
for yk in (-84.0, -71.0, -58.0):                 # the brow's: across, either side of the horns, forked at their ends
    u = AX - 14.0
    wy = yk + 3.0 * np.sin(u / 7.0)
    put(head & (u > 0) & (u < 44) & (np.abs(Y - wy) < 3.2 * (1.0 - u / 44.0)) & ~((u > 30) & (np.abs(Y - wy) < 0.7)),
        band, np.full_like(X, 0.0))

# the rhino's hide: heavy folds across the cheeks, a dark crease with a bright lip under it
for rf in (112.0, 128.0):
    rfd = np.hypot(X, Y + 62.0) - rf
    fold = head & ~ruff & (AX > 40) & (s < 0.95) & (Y > -20)
    put(fold & (np.abs(rfd) < 1.6), band, np.full_like(X, 0.0))
    put(fold & (rfd >= 1.6) & (rfd < 4.0), band, np.full_like(X, 15.0))

# the eyes: almond, slanted, ringed out in bands under a heavy brow; gold irises lined like rays, round pupils
EX, EY = 80.0, -30.0
ex0, ey0 = AX - EX, Y - EY
SL = np.radians(-14)                          # (the inner corner lower)
ex = ex0 * np.cos(SL) + ey0 * np.sin(SL)
ey = -ex0 * np.sin(SL) + ey0 * np.cos(SL)
e = np.hypot(ex / 22.0, ey / 12.5)
ring = (e >= 1.0) & (e < 1.75)
kr = np.floor((e - 1.0) / 0.19)
put(ring & head, (kr.astype(np.int16) + 2) % TURN, np.where((e - 1.0) - kr * 0.19 < 0.045, 0.0, 13.0))
put((e >= 1.0) & (e < 1.16), np.int16(0), np.full_like(X, 0.0))          # (a tiger's liner, thick and black)
put(e < 1.0, np.int16(GOLD), 9.0 + 4.0 * (0.5 + 0.5 * np.cos(np.arctan2(ey, ex) * 18)) * np.clip(e, 0.3, 1) + 2 * (e < 0.5))
put((e < 1.0) & (e > 0.9), np.int16(GOLD), np.full_like(X, 4.0))
put(np.hypot(ex, ey) < 6.5, np.int16(GOLD), np.full_like(X, 0.0))
put(np.hypot(ex + 5, ey + 4) < 2.6, np.int16(WHITE), np.full_like(X, 15.0))
bl = -11.0 - 0.50 * ex                       # the brow's lower edge: low at the inner corner, flung up outward
bh = 6.0 + 0.30 * (ex + 22.0) + 13.0 * tri((ex - 0.8 * (ey - bl)) / 8.0) ** 1.2 * np.clip((ex + 20.0) / 30.0, 0.3, 1.0)   # (flaring locks)
lid = (e < 1.16) & (ey < -3.0 - 0.36 * ex)        # the upper lid, low at the inner corner: a glare
put(lid, np.int16(0), np.full_like(X, 0.0))
brow = (ey < bl) & (ey > bl - bh) & (ex > -22) & (ex < 40)
put(brow, np.int16(WHITE), np.where(ey > bl - 1.8, 1.0, 14.0 - 3.0 * np.clip((bl - ey) / bh, 0, 1)))   # (white, as a tiger's)

# the snout: broad, plated, the nostrils at the horn's foot, a heavy lip, a tiger's fangs
sw = 24.0 + 16.0 * np.sqrt(np.clip((Y - 20.0) / 62.0, 0, 1))   # (its half-width: wide at the lip)
snout = (Y > 20) & (Y < 82) & (AX < sw)
q = np.hypot(AX / sw, (Y - 30.0) / 52.0)      # (rings out from the horn's foot, like the ears')
kq = np.floor(q * 4.5)
put(snout, (kq.astype(np.int16) + 4) % TURN, np.where(q * 4.5 - kq < 0.16, 2.0, 13.0 - 1.2 * kq))
put(snout & (AX >= sw - 1.6), np.int16(11), np.full_like(X, 1.0))
nos = np.hypot((AX - 21.0) / 4.5, (Y - 46.0 - 0.3 * (AX - 21.0)) / 8.0) < 1.0
put(nos, np.int16(0), np.full_like(X, 0.0))
my = 80.0 + 0.009 * AX * AX
put((np.abs(Y - my) < 1.5) & (AX < 44), np.int16(0), np.full_like(X, 0.0))
fang = (Y >= my) & (Y < my + 18 * (1 - np.abs(AX - 20) / 5.0)) & (np.abs(AX - 20) < 5.0)
put(fang, np.int16(WHITE), np.full_like(X, 14.0))

# the horns: the small one behind, the great one from the nose, ivory, grained along their length
for hy0, hy1, w0 in ((-34.0, -104.0, 10.0), (40.0, -58.0, 21.0)):   # (ivory, ringed: they stand out of the colours)
    hw = w0 * np.clip((Y - hy1) / (hy0 - hy1), 0, 1) ** 1.35
    horn = (Y < hy0) & (Y > hy1) & (AX < hw)
    put(horn & (AX > hw - 1.6), np.int16(0), np.full_like(X, 0.0))
    grain = 1.2 * tri(AX / np.maximum(hw, 1) * 3.0)   # (its grain, lengthwise)
    put(horn & (AX <= hw - 1.6), np.int16(WHITE), 14.5 - 7.0 * (AX / np.maximum(hw, 1)) ** 1.5 - grain)

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
