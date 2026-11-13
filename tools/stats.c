/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 hacha */
/* The structure, counted: each world played on its own (no hands) for a while, bar by bar: how long the grooves run
 * between breaks, how long the breaks are, how often the bass gallops or rolls in triplets, how much of the time the
 * line plays, a part sits out, and the events and the line's notes a bar. The engine at its defaults, Drift moved by
 * an argument. Usage: stats [minutes] [drift (-1: its default)] [world (-1: all)] */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#define PLAT_HOST
#include "../app/dsp/psy.c"

int main(int argc, char **argv)
{
    float minutes = argc > 1 ? (float)atof(argv[1]) : 30.0f;
    int drift = argc > 2 ? atoi(argv[2]) : -1, only = argc > 3 ? atoi(argv[3]) : -1, wi;
    static float l[256], r[256];
    if (only < 0)
        printf("%-16s %5s %6s %-20s %6s %6s %6s %6s %6s %6s %6s\n", "world", "bars", "drops", "groove(avg min..max)", "break",
           "gallop", "trip", "line%", "sit%", "ev/bar", "ln/bar");
    for (wi = 0; wi < PS_NWORLD; wi++) {
        if (only >= 0 && wi != only)
            continue;
        uint32_t bars = 0, last_bar = 0, n_brk = 0, brk_bars = 0, n_grv = 0, grv_bars = 0, run = 0, phr = 0, gal = 0,
                 tri = 0, ln_on = 0, sit = 0, ev0, d0, lh0, grv_bars_main = 0;
        uint32_t gmin = 1u << 30, gmax = 0, bmin = 1u << 30, bmax = 0;
        int sec_prev = SEC_MAIN;
        float lim_min = 1.0f;
        uint32_t lim_n = 0;
        double samples = 0, total = (double)minutes * 60.0 * PS_SR;
        ps_init();
        if (drift >= 0)
            ps_set(P_DRIFT, drift);
        ps_world(wi);
        ps_play(1);
        {   /* the world from the first bar: a bar played, then from the top */
            uint32_t k;
            for (k = 0; k < 400u; k++)
                ps_render(l, r, 256);
            ps_restart();
        }
        d0 = ps_ui.drops;
        ev0 = ps_ui.hits[HIT_EVENT];
        lh0 = ps_ui.hits[HIT_LINE];
        while (samples < total) {
            uint32_t b;
            ps_render(l, r, 256);
            samples += 256;
            lim_n += s.lim < 0.89f;            /* over 1 dB of the limiter */
            if (s.lim < lim_min)
                lim_min = s.lim;
            b = ps_ui.tick / (PS_TPB * 4u);
            if (b == last_bar)
                continue;
            last_bar = b;
            bars++;
            if (ps_ui.section == SEC_MAIN) {
                run++;
                grv_bars_main++;
                if (ps_ui.sec_bar == 0u) {      /* a phrase: its bass shape, its line, who sits out */
                    phr++;
                    gal += s.pat == 1u;
                    tri += s.pat == 2u;
                    ln_on += ln.playing;
                    sit += s.out != 0u;
                }
            }
            if (ps_ui.section != sec_prev) {
                if (sec_prev == SEC_MAIN && ps_ui.section == SEC_BREAK) {
                    n_grv++;
                    grv_bars += run;
                    if (run < gmin) gmin = run;
                    if (run > gmax) gmax = run;
                    run = 0;
                    n_brk++;
                }
                sec_prev = ps_ui.section;
            }
            if (ps_ui.section == SEC_BREAK)
                brk_bars++;
        }
        printf("%-16s %5u %6u %4u %5u..%-4u %6.1f %5.0f%% %5.0f%% %5.0f%% %5.0f%% %6.1f %6.1f\n", ps_world_name(wi), bars,
               (uint8_t)(ps_ui.drops - d0), n_grv ? grv_bars / n_grv : 0, n_grv ? gmin : 0, gmax, n_brk ? (double)brk_bars / n_brk : 0.0,
               phr ? 100.0 * gal / phr : 0.0, phr ? 100.0 * tri / phr : 0.0, phr ? 100.0 * ln_on / phr : 0.0,
               phr ? 100.0 * sit / phr : 0.0, (double)(ps_ui.hits[HIT_EVENT] - ev0) / bars,
               (double)(ps_ui.hits[HIT_LINE] - lh0) / bars);
        printf("%-16s the limiter: over 1 dB %.1f%% of the time, at most %.1f dB\n", "", 100.0 * lim_n * 256.0 / samples,
               -20.0 * log10f(lim_min));
        (void)bmin; (void)bmax; (void)grv_bars_main;
    }
    return 0;
}
