/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 hacha */
/* RhinoTiger's line (psy.c includes this; its world, rnd(), note_hz(), m and s): a repeating phrase of two bars of
 * sixteenths over the bass, a two-operator FM voice, mono.
 *
 * The phrase: 32 steps, each on or not (leaning to the beat's offbeat sixteenths, Density), its note (a walk in the
 * Phrygian scale over the world's register, or a semitone's creep: the world's, and Dark more), an accent, a slide to
 * the next. Every 4 bars Mutate (and Drift) rewrites a few steps; a drop draws it anew. It comes in after the intro,
 * and as each phrase (8 bars) begins it plays or rests by the world's presence. In a break it stays, softer.
 *
 * The voice: the modulation falls from the accent's height to the world's floor (a wah in some worlds: it swings with
 * the note); the pitch glides on a slide; the note's length is Length's share of its step. */

#define LN_STEPS 32u
static struct {
    int8_t note[LN_STEPS];             /* semitones over the register */
    uint8_t on[LN_STEPS], acc[LN_STEPS], slide[LN_STEPS];
    uint8_t playing;                   /* this phrase it plays */
    /* the voice */
    float t, gate;                     /* seconds since the note (< 0: silent), its length */
    float cph, mph, inc, inc_to;       /* carrier and modulator phase (turns), the step, gliding to */
    float idx, idx_k, idx_floor;       /* the modulation now, its fall a sample, its floor */
    float wah_ph;
    float lp;
    float pan;
} ln;

static const int8_t PHRYGIAN[7] = {0, 1, 3, 5, 7, 8, 10};

static int ln_scale_note(int deg)      /* a degree (any whole number) of the Phrygian scale, in semitones */
{
    int o = deg >= 0 ? deg / 7 : -((6 - deg) / 7);
    return o * 12 + PHRYGIAN[deg - o * 7];
}

static void ln_draw_step(const world_t *w, uint32_t i)   /* step i anew */
{
    static const float WEIGHT[4] = {0.25f, 0.85f, 1.0f, 0.8f};   /* in the beat: on the kick, then the offbeats */
    float dens = w->line_dens * (float)par[P_LINE_DENSITY] / 50.0f * (1.0f + 0.4f * m.energy);
    int prev = ln.note[(i + LN_STEPS - 1u) % LN_STEPS], range = w->line_range + (m.dark > 0.0f ? (int)(6.0f * m.dark) : 0);
    int chrom = clampi((int)w->line_chrom + (int)(40.0f * m.dark), 0, 100), n;
    ln.on[i] = (float)(rnd() % 1000u) < 1000.0f * dens * WEIGHT[i % 4u];
    if ((int)(rnd() % 100u) < chrom)   /* a semitone's creep from the step before */
        n = prev + ((rnd() & 1u) ? 1 : -1);
    else {                             /* a step or two along the scale, or back to the root */
        int r = (int)(rnd() % 10u);
        n = r < 2 ? 0 : ln_scale_note((int)(rnd() % 5u) - 2 + (r < 6 ? 0 : (int)(rnd() % 3u)));
        if (rnd() % 3u == 0u)
            n = prev + (n - prev) / 2;
    }
    ln.note[i] = (int8_t)clampi(n, -2, range);
    ln.acc[i] = rnd() % 100u < 25u;
    ln.slide[i] = (int)(rnd() % 100u) < (int)w->line_slide;
}

static void ln_draw(const world_t *w)
{
    uint32_t i;
    for (i = 0; i < LN_STEPS; i++)
        ln_draw_step(w, i);
}

/* a tick: rb the bar since the last drop, the step in the phrase */
static void ln_step(const world_t *w, uint32_t t, uint32_t bar, uint32_t rb, uint32_t pos16)
{
    uint32_t i = pos16 % LN_STEPS;
    float step_s = s.tick_len * 6.0f / PS_SR;
    if (t % 6u)
        return;
    if (pos16 % 16u == 0u && t == 0u) {
        if (rb == 0u && (pos16 == 0u))
            ln_draw(w);                /* the start, a drop: anew */
        else if (rb % 4u == 0u) {      /* every 4 bars: Mutate rewrites a few */
            int k, n = (int)((float)par[P_LINE_MUTATE] / 100.0f * 6.0f * (0.5f + m.drift) + (float)(rnd() % 100u) / 100.0f);
            for (k = 0; k < n; k++)
                ln_draw_step(w, rnd() % LN_STEPS);
        }
        if (rb % 8u == 0u) {           /* a phrase: plays or rests, by the world's presence (more likely after a rest;
                                        * where it is half or more, surely as the intro ends) */
            float p = w->line + (ln.playing ? 0.0f : 0.3f * w->line);
            ln.playing = bar >= 16u && ((bar < 24u && w->line >= 0.5f) || (float)(rnd() % 100u) < 100.0f * p);
        }
    }
    if (!ln.playing || !ln.on[i] || (s.sec == SEC_BUILD && s.sec_bar >= 3u && s.tick / TPB % 4u == 3u))
        return;
    {
        float f = note_hz(m.root + w->line_oct + ln.note[i]) / PS_SR;
        int glide = ln.slide[(i + LN_STEPS - 1u) % LN_STEPS] && ln.t >= 0.0f;
        ln.inc_to = f;
        if (!glide) {
            ln.inc = f;
            ln.cph = ln.mph = 0.0f;
        }
        ln.t = 0.0f;
        ln.gate = step_s * (ln.slide[i] ? 1.05f : w->line_gate * (0.4f + 1.2f * (float)par[P_LINE_LEN] / 100.0f));
        ln.idx = w->line_index * (ln.acc[i] ? 1.5f : 1.0f) * (1.0f + 0.4f * m.dark);
        ln.idx_floor = w->line_index * 0.15f;
        ln.idx_k = fm_expf(-1000.0f / (w->line_ms * PS_SR));
        ln.pan = (i % 2u) ? 0.15f : -0.15f;
        ps_ui.hits[HIT_LINE]++;
    }
}

static float ln_render(const world_t *w, float dt, float *l, float *r)   /* returns the send */
{
    float env, x, mo, idx;
    if (ln.t < 0.0f)
        return 0.0f;
    if (ln.t < ln.gate)
        env = ln.t < 0.002f ? ln.t / 0.002f : 1.0f;
    else {
        env = 1.0f - (ln.t - ln.gate) / 0.006f;
        if (env <= 0.0f) {
            ln.t = -1.0f;
            return 0.0f;
        }
    }
    ln.inc += (ln.inc_to - ln.inc) * 0.004f;   /* a slide: ~5 ms */
    ln.idx = ln.idx_floor + (ln.idx - ln.idx_floor) * ln.idx_k;
    idx = ln.idx;
    if (w->line_wah > 0.0f) {          /* the wah: the modulation swinging with the note */
        if ((ln.wah_ph += 7.0f * dt) >= 1.0f)
            ln.wah_ph -= 1.0f;
        idx *= 1.0f - w->line_wah * (0.5f + 0.5f * fm_sin_turns(ln.wah_ph));
    }
    mo = fm_sin_turns(ln.mph);
    x = fm_sin_turns(ln.cph + idx * mo * 0.159155f) * env;
    if ((ln.cph += ln.inc) >= 1.0f)
        ln.cph -= 1.0f;
    if ((ln.mph += ln.inc * w->line_ratio) >= 1.0f)
        ln.mph -= 1.0f;
    ln.t += dt;
    ln.lp += (x - ln.lp) * 0.6f;
    x = ln.lp * 0.22f;
    *l += x * (1.0f - ln.pan);
    *r += x * (1.0f + ln.pan);
    return x;
}
