#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 hacha
# The boot's characters (app/logo.h): 犀 and 虎 from Noto Sans JP at its heaviest (900; SIL Open Font License 1.1,
# Google), each fitted into LOGO_PX square (its ink), drawn four times as large and taken down (its edges' coverage in
# 16 levels), and a ring around it (the ink grown RING pixels, its edge soft) to set it off what is behind.
# Usage: tools/kanji.py [font]     (writes app/logo.h; default ~/Library/Fonts/NotoSansJP-VariableFont_wght.ttf)
import os, sys
from PIL import Image, ImageDraw, ImageFont, ImageFilter
HERE = os.path.dirname(os.path.abspath(__file__))
FONT = sys.argv[1] if len(sys.argv) > 1 else os.path.expanduser('~/Library/Fonts/NotoSansJP-VariableFont_wght.ttf')
PX, SS, RING = 112, 4, 2
def glyph(ch):
    f = ImageFont.truetype(FONT, PX * SS)
    try:
        f.set_variation_by_axes([900])
    except Exception:
        pass
    im = Image.new('L', (PX * SS * 2, PX * SS * 2), 0)
    ImageDraw.Draw(im).text((PX * SS // 2, PX * SS // 2), ch, font=f, fill=255)
    im = im.crop(im.getbbox())
    k = PX * SS / max(im.size)               # the ink fitted in the square, kept in proportion and centred
    im = im.resize((max(1, round(im.size[0] * k)), max(1, round(im.size[1] * k))), Image.LANCZOS)
    sq = Image.new('L', (PX * SS, PX * SS), 0)
    sq.paste(im, ((PX * SS - im.size[0]) // 2, (PX * SS - im.size[1]) // 2))
    a = sq.resize((PX, PX), Image.BOX)
    ring = a.point(lambda v: 255 if v > 96 else 0).filter(ImageFilter.MaxFilter(2 * RING + 1)).filter(ImageFilter.BoxBlur(0.6))
    return a, ring
out = []
for ch in '犀虎':
    a, r = glyph(ch)
    for y in range(PX):
        for x in range(PX):
            out.append(((a.getpixel((x, y)) * 15 + 127) // 255) << 4 | ((r.getpixel((x, y)) * 15 + 127) // 255))
rle = []
i = 0
while i < len(out):
    n = 1
    while i + n < len(out) and out[i + n] == out[i] and n < 255:
        n += 1
    rle += [out[i], n]
    i += n
L = ['/* SPDX-License-Identifier: GPL-3.0-only',
     ' * Copyright (C) 2026 hacha */',
     '/* The boot logo\'s characters: 犀 over 虎 (RhinoTiger), LOGO_PX square each, from Noto Sans JP at its heaviest',
     ' * (SIL Open Font License 1.1, Google). Made by tools/kanji.py; not to be edited by hand. Pixel by pixel, row by row,',
     ' * the one then the other: the character\'s coverage (high four bits, 0..15) and its dark ring\'s (low four), run-',
     ' * length coded in pairs: the value, then its run (1..255). */',
     '#define LOGO_PX %d' % PX,
     '#define LOGO_RLE_N %d' % len(rle),
     'static const uint8_t LOGO_RLE[LOGO_RLE_N] = {']
for k in range(0, len(rle), 24):
    L.append('    ' + ', '.join('0x%02X' % b for b in rle[k:k + 24]) + ',')
L.append('};')
open(os.path.join(HERE, '..', 'app', 'logo.h'), 'w').write('\n'.join(L) + '\n')
Image.merge('RGB', (a, r, Image.new('L', a.size))).resize((PX * 3, PX * 3), Image.NEAREST).save(os.path.join(HERE, 'kanji.png'))
print('app/logo.h:', len(rle), 'bytes')
