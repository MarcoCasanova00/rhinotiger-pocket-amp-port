# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 hacha
# Clicks: a sample whose second difference stands far above its neighbourhood's (a lone discontinuity, not noise).
import sys, wave, array, math
def clicks(path, k=10.0, win=96):
    w = wave.open(path); sr = w.getframerate()
    a = array.array('h', w.readframes(w.getnframes()))
    out = []
    for ch in (0, 1):
        x = a[ch::2]; n = len(x)
        d = [0.0]*n
        for i in range(1, n-1):
            d[i] = x[i+1] - 2*x[i] + x[i-1]
        # running mean of |d| over a window, excluding the centre's +-2
        cs = [0.0]
        for v in d: cs.append(cs[-1] + abs(v))
        for i in range(win, n-win):
            v = abs(d[i])
            if v < 400: continue
            loc = (cs[i+win] - cs[i-win] - (cs[i+3]-cs[i-2])) / (2*win - 5)
            if v > k * (loc + 20):
                out.append((i/sr, ch, v, loc))
    # merge near ones
    out.sort(); m = []
    for t, ch, v, loc in out:
        if m and t - m[-1][0] < 0.005: continue
        m.append((t, ch, v, loc))
    return m
if __name__ == '__main__':
    for p in sys.argv[1:]:
        c = clicks(p)
        print(f"{p.split('/')[-1]:34s} {len(c):4d}", ' '.join('%.3f' % t for t, *_ in c[:8]))
