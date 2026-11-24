// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 hacha
/* AUTO PLAY: a set, played as a DJ would: 30, 60 or 90 minutes, or without end. Included in app.c; on the UI's side, it
 * only turns what a hand could (psy.h), a bar at a time.
 *
 * A set is drawn from its seed: a curve of how high it stands (one of several, none chosen by hand), and chapters
 * of some 4 to 10 minutes along it, each a track: a world, a tempo reached by its end, a key, the macros, how dark.
 * A chapter is a few grooves of 32 to 160 bars (its last what is left), each ending its own way: its one full break
 * (a long break, an 8-bar build, the drop), or a short break and the drop at once, a build straight from the groove,
 * or the groove alone, a part out or a filter into a drop of its own. The highest chapter is the set's peak: its break
 * the longest, the chapter before it held back. The macros climb to a chapter's full break and stand highest after it.
 *
 * Chapters change one of three ways, as a DJ goes from one track to the next: through the last full break, the world
 * and the key coming with the drop (A); into the next one's break, the world and the key coming as the break begins
 * (B); or in the groove, the old one thinned (the low end out, the bass, the kick) into a drop that brings the new
 * one, which comes in without its line and its events, back one by one (C). The world and the key change there,
 * never elsewhere. The tempo glides, a tenth of a BPM a bar at most, and goes on through the change of world; the
 * macros move one step a bar. The tempo when it is turned on is where the set begins, and all its tempos move with it
 * (125 to 206).
 *
 * What a hand turns is the hand's for AU_HOLD bars, then glides back (the key: at the next chapter; a world: for its
 * chapter). A break or a build called by hand is followed. The settings kept (set) are the hand's alone: SAVE during a
 * set keeps those. Off: the music goes on from where it is, the knobs where the set left them. A set done: its last
 * break fades, the music rests, and everything is the hand's again.
 *
 * Its hands (the effects, the parts out, the keys): hands.h, as much as PERFORM (K4).
 *
 * Its dice are its own (never the engine's): the same seed draws the same set; what the generator plays within it is
 * its own each time. */
#define AU_HOLD 32                       /* bars a thing touched stays the hand's */
#define AU_NCH 20
#define AU_FADE_BARS 12
#define AU_NSEG 6
enum { AU_LEN_30, AU_LEN_60, AU_LEN_90, AU_LEN_ENDLESS, AU_NLEN };
enum { MIX_START, MIX_A, MIX_B, MIX_C };   /* how a chapter comes in: the set's first; A, B, C as above */
/* how a groove ends: its chapter's full break (a break, an 8-bar build, the drop); a short break, the drop at once; a
 * build straight from it; a drop of its own after a part out or a filter (ROLL); into the next chapter's break (B); a
 * drop bringing the next chapter, the old one thinned (C) */
enum { K_MAIN, K_SHORT, K_BUILD, K_ROLL, K_B, K_C, K_N };
typedef struct {
    uint8_t world, root;
    uint8_t mac[4];                      /* P_ENERGY .. P_DRIFT */
    uint8_t high;                        /* how high it stands, 0..100 */
    uint8_t mix, peak;                   /* how it comes in (MIX_*); the set's highest */
    float bpm;                           /* reached by its end */
    float end;                           /* minutes from the set's start (endless: of its cycle) */
} au_ch_t;
typedef struct {
    uint8_t kind, len;                   /* how it ends (K_*); a break's bars (ROLL: which of its edges) */
    uint16_t groove;                     /* bars */
} au_seg_t;
static struct {
    uint8_t on, len, nset, cycle;
    uint8_t nch, cur, last;              /* the chapters, the one now; last: this groove is its chapter's last */
    uint8_t nseg, seg, kind, mi;         /* the chapter's grooves, the next one's; how the one now ends (K_*); its full break's */
    uint8_t cued, edged, intro;          /* its end cued; its edge planned; the set's beginning to come in by degrees */
    uint8_t main_done;                   /* the chapter's full break has come */
    au_seg_t f[AU_NSEG];
    float um, min_main;                  /* the full break's place in the chapter (0..1, planned); the minute it came */
    uint8_t begun;                       /* bars have been counted (the music plays) */
    uint8_t ours, staged, closing;       /* the break now is the set's; the next world waits for its drop; the fade */
    uint32_t rng;
    au_ch_t ch[AU_NCH];
    float from;                          /* the tempo as the chapter began */
    float bpm, start, min;               /* now; the chapter's start; since the set's (its cycle's) start */
    float base;                          /* the tempo when it was turned on: where the set begins, all its tempos moved with it */
    float total;                         /* minutes before this cycle */
    int bar, drop_bar, sec, sec_at, groove, brk;
    uint8_t drops;
    int hold[P_NPARAMS];                 /* till this bar the hand's */
    float val[4];                        /* the macros, gliding */
} au;
static uint32_t au_rested;               /* the set over: silent till this (ms), then whole again */

static uint32_t au_rnd(uint32_t n)       /* 0..n-1 */
{
    au.rng ^= au.rng << 13;
    au.rng ^= au.rng >> 17;
    au.rng ^= au.rng << 5;
    return (au.rng >> 8) % n;
}
static float au_rndf(float lo, float hi) { return lo + (hi - lo) * (float)au_rnd(1024u) / 1023.0f; }
static int au_owns(int p) { return au.on && (p <= P_DRIFT || p == P_TEMPO || p == P_ROOT || p == P_BREAKS || p == P_BUILD_LEN); }
static int au_held(int p) { return au.bar < au.hold[p]; }
static int au_breaky(void) { return au.kind == K_MAIN || au.kind == K_SHORT || au.kind == K_B; }   /* (it ends in a break) */

#include "hands.h"                        /* its effects, its parts out, its keys */

/* how high the set stands at x (0..1): a curve's points, straight between */
#define AU_NCURVE 6
static float au_curve(uint32_t c, float x)
{
    static const uint8_t C[AU_NCURVE][6] = {   /* at x = 0, 1/5 .. 1, in percent */
        {15, 35, 55, 75, 95, 60},        /* a long climb */
        {25, 60, 40, 65, 100, 55},       /* two peaks, the second higher */
        {20, 40, 75, 50, 85, 65},        /* up early, a valley, up again */
        {30, 50, 45, 70, 60, 90},        /* in steps, the highest last */
        {40, 70, 85, 55, 40, 75},        /* high early, a deep middle, back */
        {15, 25, 45, 85, 100, 80},       /* slow, then all at once */
    };
    float f = fm_clampf(x, 0.0f, 1.0f) * 5.0f;
    uint32_t i = f >= 5.0f ? 4u : (uint32_t)f;
    f -= (float)i;
    return ((float)C[c][i] + ((float)C[c][i + 1u] - (float)C[c][i]) * f) / 100.0f;
}

static int au_key(int root, int up)      /* the next key from root: near ones, never outside E flat .. A (27..33) */
{
    static const int8_t STEP[10] = {5, -5, 5, -5, 2, 1, -2, -3, 3, 0};
    int i, r;
    for (i = 0; i < 16; i++) {
        int s = STEP[au_rnd(10u)];
        if (up && (s == -2 || s == -3) && au_rnd(2u))
            s = s == -2 ? 2 : 1;         /* (rising: more often up) */
        r = root + s;
        if (r >= 27 && r <= 33)
            return r;
    }
    return root;
}

/* the chapters of a set (or of an endless one's next hour): go_on 1, the first is the world, the key and the tempo given;
 * 2, it goes on from them into another. The first chapter's tempo is the base (au.base), and every other moves as far as
 * the first did to get there (an endless set's next hour too) */
static void au_plan(int go_on, int world, int root, float bpm)
{
    static const uint8_t MIN[AU_NLEN] = {30, 60, 90, 60};
    float len = (float)MIN[au.len], sum = 0.0f, w[AU_NCH], bot, top, d0, d1, d2, drift, t = 0.0f, s = 0.0f;
    uint32_t curve, i, k, used[PS_NWORLD] = {0};
    int prev = go_on ? world : -1, prev2 = -1;
    au.rng = (0x9E3779B9u * (au.nset + 1u)) ^ (0x85EBCA6Bu * (au.len + 1u)) ^ (0xC2B2AE35u * (au.cycle + 1u));
    for (i = 0; i < 8u; i++)
        au_rnd(2u);
    curve = au_rnd(AU_NCURVE);
    bot = au_rndf(138.0f, 148.0f);
    top = au_rndf(164.0f, 194.0f);
    d0 = au_rndf(40.0f, 60.0f);          /* how dark: begins, its middle, its end */
    d1 = au_rndf(45.0f, 78.0f);
    d2 = au_rndf(38.0f, 65.0f);
    drift = au_rndf(45.0f, 65.0f);
    au.nch = (uint8_t)(len / au_rndf(6.0f, 7.4f) + 0.5f);
    for (i = 0; i < au.nch; i++)
        sum += w[i] = au_rndf(0.8f, 1.25f);
    for (i = 0; i < au.nch; i++) {
        au_ch_t *c = &au.ch[i];
        float x = au.nch > 1u ? (float)i / (float)(au.nch - 1u) : 0.0f, h, best = 1e9f, want;
        h = fm_clampf(au_curve(curve, x) + au_rndf(-0.06f, 0.06f), 0.05f, 1.0f);
        c->high = (uint8_t)(100.0f * h);
        t += len * w[i] / sum;
        c->end = t;
        want = bot + (top - bot) * h * (0.75f + 0.25f * h);
        if (!i)                          /* (how far the base moves them all) */
            s = au.base - fm_clampf(want, 140.0f, 190.0f);
        want += s;
        if (i || go_on)                  /* a chapter never moves it far, and down less than up */
            want = fm_clampf(want, bpm - 8.0f, bpm + 14.0f);
        want = fm_clampf(want, fm_maxf(125.0f, 140.0f + s), fm_minf(206.0f, 190.0f + s));   /* (all the worlds' reach) */
        if (!i && go_on < 2)
            want = au.base;              /* (the set begins where it was turned on) */
        c->bpm = want;
        if (!i && go_on == 1) {          /* (from where the music is) */
            c->world = (uint8_t)world;
            c->root = (uint8_t)root;
        } else {
            float mid = i || go_on ? 0.5f * (bpm + want) : want;
            for (k = 0; k < PS_NWORLD; k++) {   /* a world near its tempo, not the last two's, the less used first */
                float d = mid - (float)ps_world_bpm((int)k), q = (d < 0.0f ? -d : d) + 7.0f * (float)used[k] + au_rndf(0.0f, 10.0f) +
                          ((int)k == prev ? 200.0f : (int)k == prev2 ? 8.0f : 0.0f);
                if (d > 14.0f || d < -14.0f)
                    q += 40.0f;
                if (q < best) {
                    best = q;
                    c->world = (uint8_t)k;
                }
            }
            c->root = (uint8_t)(i || go_on ? au_key(i ? au.ch[i - 1u].root : root, !i || c->high >= au.ch[i - 1u].high)
                                           : ps_world_root(c->world));
        }
        bpm = want;
        prev2 = prev;
        prev = c->world;
        used[c->world]++;
        c->mac[0] = (uint8_t)fm_clampf(30.0f + 56.0f * h + au_rndf(-6.0f, 6.0f), 15.0f, 92.0f);
        c->mac[1] = (uint8_t)fm_clampf(34.0f + 46.0f * h + au_rndf(-8.0f, 8.0f), 15.0f, 90.0f);
        c->mac[2] = (uint8_t)fm_clampf((x < 0.5f ? d0 + (d1 - d0) * 2.0f * x : d1 + (d2 - d1) * (2.0f * x - 1.0f)) +
                                       au_rndf(-7.0f, 7.0f), 20.0f, 88.0f);
        c->mac[3] = (uint8_t)fm_clampf(drift + au_rndf(-6.0f, 6.0f), 25.0f, 80.0f);
    }
    /* the peak, and how each chapter comes in (drawn after the rest: the worlds, the keys and the tempos as they were):
     * into the peak with a break (B more often than A); out of it in the groove (C); rising, a break as often as not;
     * else mostly C. Never three the same way at once (the peak's and the one after it aside: those as said) */
    for (i = k = 0; i < au.nch; i++)
        if (au.ch[i].high > au.ch[k].high)
            k = i;
    for (i = 0; i < au.nch; i++) {
        au_ch_t *c = &au.ch[i];
        c->peak = (uint8_t)(i == k);
        if (!i) {
            c->mix = go_on == 2 ? MIX_A : MIX_START;   /* (endless: the hour's last chapter ends through its break) */
        } else if (i == k) {
            c->mix = au_rnd(5u) < 3u ? MIX_B : MIX_A;
        } else if (i == k + 1u) {
            c->mix = MIX_C;
        } else {
            uint32_t up = c->high > au.ch[i - 1u].high + 8u, a = up ? 35u : 25u, b = up ? 35u : 20u, r = au_rnd(100u);
            c->mix = r < a ? MIX_A : r < a + b ? MIX_B : MIX_C;
            if (i >= 2u && c->mix == au.ch[i - 1u].mix && c->mix == au.ch[i - 2u].mix)
                c->mix = c->mix == MIX_C ? MIX_A : MIX_C;
        }
    }
    if (k)                               /* (before the peak: held back) */
        for (i = 0; i < 2u; i++)
            au.ch[k - 1u].mac[i] = (uint8_t)fm_maxf((float)au.ch[k - 1u].mac[i] - 6.0f, 15.0f);
#ifdef PLAT_HOST
    if (host_auto_log)
        for (i = 0; i < au.nch; i++)
            printf("PLAN %d ch %d/%d curve %d world %d root %d bpm %.1f high %d end %.1f mac %d %d %d %d mix %d peak %d\n", au.nset,
                   (int)i, au.nch, (int)curve, au.ch[i].world, au.ch[i].root, (double)au.ch[i].bpm, au.ch[i].high,
                   (double)au.ch[i].end, au.ch[i].mac[0], au.ch[i].mac[1], au.ch[i].mac[2], au.ch[i].mac[3], au.ch[i].mix,
                   au.ch[i].peak);
#endif
}

static float au_goal(int k)              /* macro k's place now: its chapter's; ENERGY and DETAIL climbing to its full
                                          * break, highest after it, then settling a little */
{
    const au_ch_t *c = &au.ch[au.cur];
    float span = c->end - au.start, u = span > 0.0f ? fm_clampf((au.min - au.start) / span, 0.0f, 1.0f) : 1.0f, off = 0.0f;
    if (k < 2) {
        if (!au.main_done) {
            off = -6.0f + 12.0f * fm_minf(u / fm_maxf(au.um, 0.1f), 1.0f);
        } else {
            float m = span > 0.0f ? fm_clampf((au.min_main - au.start) / span, 0.0f, 1.0f) : 1.0f;
            off = 8.0f - 5.0f * (m < 0.99f ? fm_clampf((u - m) / (1.0f - m), 0.0f, 1.0f) : 1.0f);
        }
    }
    return fm_clampf((float)c->mac[k] + off, 0.0f, 100.0f);
}
static void au_push(void)                /* what the set holds, to the engine (and where the knobs stand) */
{
    int k;
    for (k = 0; k < 4; k++)
        if (!au_held(P_ENERGY + k))
            ps_set(P_ENERGY + k, (int)(au.val[k] + 0.5f));
    if (!au_held(P_BREAKS))
        ps_set(P_BREAKS, 0);             /* (its breaks are the set's) */
    ps_auto_bpm(au.bpm);
    ps_set(P_TEMPO, clamp((int)(au.bpm + 0.5f) - ps_world_bpm(ps_world_now()), ps_param(P_TEMPO)->lo, ps_param(P_TEMPO)->hi));
    ps_set(P_ROOT, clamp(ps_root() - ps_world_root(ps_world_now()), ps_param(P_ROOT)->lo, ps_param(P_ROOT)->hi));
}
static int au_tail(const au_seg_t *f)    /* the bars after a groove till its drop (a build: 4, give or take) */
{
    return f->kind == K_MAIN ? f->len + 8 : f->kind == K_SHORT ? f->len : f->kind == K_BUILD ? 4 : 0;
}

/* the chapter's form: its grooves and how each ends. The last as the next chapter comes in (A: the full break; the
 * set's end and an endless hour's: the same); the full break about two thirds in otherwise; the others a short break,
 * a build or a drop of its own, by how high the chapter stands, not the same twice running, one short break at most */
static void au_form(void)
{
    const au_ch_t *c = &au.ch[au.cur];
    int fin = au.cur + 1u < au.nch ? au.ch[au.cur + 1u].mix : MIX_A, n, mi, i, prev = -1, shorts = 0, h = c->high;
    float len = (c->end - au.min) * au.bpm / 4.0f - (c->mix == MIX_B ? 24.0f : 0.0f), at = 0.0f, sum = 0.0f, q;
    n = clamp((int)(len / 100.0f + 0.5f), 2, AU_NSEG);
    mi = fin == MIX_A ? n - 1 : clamp((int)(0.65f * (float)n + 0.5f) - 1, 0, n - 2);
    for (i = 0; i < n; i++) {
        au_seg_t *f = &au.f[i];
        f->groove = (uint16_t)(16 * (3 + (int)au_rnd(6u)));
        if (i == n - 1 && fin != MIX_A) {
            f->kind = fin == MIX_B ? K_B : K_C;
            f->len = 16;
        } else if (i == mi) {
            f->kind = K_MAIN;
            f->len = (uint8_t)(c->peak ? 32 : au_rnd(2u) ? 24 : 16);
        } else {
            int w[3] = {shorts ? 0 : 20 + h / 5, 10 + h / 3, 50 - h / 4}, r, k;
            if (prev >= 0)
                w[prev] = 0;
            r = (int)au_rnd((uint32_t)(w[0] + w[1] + w[2]));
            for (k = 0; k < 2 && r >= w[k]; k++)
                r -= w[k];
            prev = k;
            f->kind = (uint8_t)(K_SHORT + k);
            f->len = (uint8_t)(f->kind == K_SHORT ? (au_rnd(2u) ? 8 : 4) : au_rnd(5u));
            shorts += f->kind == K_SHORT;
        }
        sum += (float)(f->groove + au_tail(f));
    }
    q = sum > 0.0f ? len / sum : 1.0f;   /* (the grooves to the chapter's length) */
    for (i = 0; i < n; i++) {
        au_seg_t *f = &au.f[i];
        f->groove = (uint16_t)clamp((int)((float)f->groove * q / 16.0f + 0.5f) * 16, 32, 160);
        if (i < mi)
            at += (float)(f->groove + au_tail(f));
        else if (i == mi)
            at += (float)f->groove;
    }
    au.nseg = (uint8_t)n;
    au.mi = (uint8_t)mi;
    au.seg = 0;
    au.um = len > 0.0f ? fm_clampf(at / len, 0.1f, 1.0f) : 1.0f;
}
static void au_segment(void)             /* a groove begins: its form's next, how long, how it ends. The chapter's last
                                          * (planned, or no room left for more) takes what is left */
{
    const au_ch_t *c = &au.ch[au.cur];
    float left = (c->end - au.min) * au.bpm / 4.0f;   /* bars */
    int i = au.seg < au.nseg ? au.seg : au.nseg - 1;
    if (i < au.nseg - 1 && left < (float)(au.f[i].groove + au_tail(&au.f[i]) + 48))
        i = i < au.mi && !au.main_done ? au.mi : au.nseg - 1;   /* (short of time: on to the full break, else the last) */
    au.seg = (uint8_t)(i + 1);
    au.kind = au.f[i].kind;
    au.brk = au.f[i].len;
    au.groove = au.f[i].groove;
    au.cued = au.edged = 0;
    au.last = (uint8_t)(i == au.nseg - 1);
    if (au.last)
        au.groove = clamp(((int)left - (au.kind == K_MAIN ? au.brk + 8 : 0) + 4) / 8 * 8, au.kind == K_C ? 48 : 32, 160);
}
static void au_chapter(int i)            /* chapter i begins (on its drop; B: as the break leading to it begins) */
{
    au.cur = (uint8_t)i;
    au.start = au.min;
    au.from = au.bpm;
    au.staged = 0;
    au.last = au.main_done = 0;
    au_form();
}
static int au_more(void) { return au.len == AU_LEN_ENDLESS || au.cur + 1u < au.nch; }

static void au_top(void)                 /* from the set's own beginning (nothing plays yet) */
{
    int k;
    au.cycle = 0;
    au.total = au.min = 0.0f;
    au.closing = au.staged = au.ours = 0;
    if (au_fade < 1.0f)                  /* (rested in the fade: whole again once the echoes are gone) */
        au_rested = plat_ms() | 1u;
    ps_stage(-1, 0);
    au_plan(0, 0, 0, 0.0f);
    au.bpm = au.ch[0].bpm;
    au_chapter(0);
    for (k = 0; k < 4; k++)
        au.val[k] = (float)au.ch[0].mac[k] - (k < 2 ? 6.0f : 0.0f);
    for (k = 0; k < P_NPARAMS; k++)
        au.hold[k] = 0;
    au.bar = au.drop_bar = 0;
    au.intro = 1;
    au.sec = SEC_MAIN;
    au.drops = ps_ui.drops;
    au.begun = 0;
    ps_world(au.ch[0].world);
    ps_auto_root(au.ch[0].root);
    au_push();
    au_segment();
    hd_begin();
}
static int au_bar_now(void) { uint32_t t = ps_ui.tick; return t < 4u ? 0 : (int)((t - 4u) / (4u * PS_TPB)); }
static void au_start(void)
{
    uint32_t k;
    au.on = 1;
    au.len = set.auto_len;
    au.nset = set.auto_set ? set.auto_set : (uint8_t)(1u + (plat_ms() * 2654435761u >> 16) % 99u);
    au_fade = 1.0f;
    au_rested = 0;
    au.base = (float)clamp(ps_bpm(), 125, 206);   /* (a clock's tempo too, within the worlds' reach) */
    if (!ps_playing() || boot_hold) {
        au_top();
        return;
    }
    au.cycle = 0;                        /* playing: from where the music is, its first chapter */
    au.intro = 0;
    au.total = au.min = 0.0f;
    au.closing = au.staged = au.ours = 0;
    au.bpm = au.base;
    au_plan(1, ps_world_now(), ps_root(), au.bpm);
    au_chapter(0);
    for (k = 0; k < 4u; k++)
        au.val[k] = (float)ps_get(P_ENERGY + (int)k);
    for (k = 0; k < P_NPARAMS; k++)
        au.hold[k] = 0;
    au.bar = au_bar_now();
    au.sec = ps_section();
    au.drop_bar = au.bar - (au.sec == SEC_MAIN ? ps_ui.sec_bar : 0);   /* (the phrases the engine's) */
    au.sec_at = au.bar;
    au.drops = ps_ui.drops;
    au.begun = 1;
    ps_auto_root(ps_root());
    au_push();
    au_segment();
    hd_begin();
}
static void au_stop(int keep)            /* off: keep, the music goes on, the knobs where they stand; else the hand's */
{
    int k;
    if (!au.on)
        return;
    au.on = 0;
    hd_clear(255u);
    ps_stage(-1, 0);
    if (keep) {
        for (k = P_ENERGY; k <= P_DRIFT; k++)
            set.par[k] = (int16_t)ps_get(k);
        set.par[P_TEMPO] = (int16_t)ps_get(P_TEMPO);
        set.par[P_ROOT] = (int16_t)ps_get(P_ROOT);
        set.world = (uint8_t)ps_world_now();
        set_dirty = 1;
    }
    for (k = 0; k < P_NPARAMS; k++)
        ps_set(k, set.par[k]);
    ps_world(set.world);
    ps_auto_root(0);
    ps_auto_bpm(0.0f);                   /* (a fade: au_tick brings it back) */
}

/* a knob of the set's turned by hand: from where it stands, the hand's a while */
static int au_turn(int p, int step)
{
    int v, w = ps_world_now();
    au.hold[p] = au.bar + AU_HOLD;
    if (p == P_TEMPO) {
        au.bpm = fm_clampf(au.bpm + (float)step, 125.0f, 206.0f);   /* (all the worlds' reach) */
        au.bpm = (float)(int)(au.bpm + 0.5f);
        au_push();
        return 1;
    }
    if (p == P_ROOT) {
        v = clamp(ps_root() + step, ps_world_root(w) + ps_param(p)->lo, ps_world_root(w) + ps_param(p)->hi);
        ps_auto_root(v);
        au_push();
        return 1;
    }
    v = clamp(ps_get(p) + step, ps_param(p)->lo, ps_param(p)->hi);
    if (v == ps_get(p))
        return 0;
    ps_set(p, v);
    if (p <= P_DRIFT)
        au.val[p - P_ENERGY] = (float)v;
    return 1;
}

static uint32_t au_put2(char *t, uint32_t v)   /* two digits */
{
    t[0] = (char)('0' + v / 10u % 10u);
    t[1] = (char)('0' + v % 10u);
    t[2] = 0;
    return 2;
}
/* the set's time so far, minutes (an endless one's since its start, au.total its hours before): smooth through a bar
 * (au.min moves a bar at a time; the tempo as au_tick counts the bars, so no second back at one) */
static float au_elapsed(int all)
{
    uint32_t tk = ps_ui.tick;
    float m = au.min, bpm = (float)ps_bpm();
    if (bpm <= au.bpm + 1.0f && bpm >= au.bpm - 1.0f)
        bpm = au.bpm;
    if (au.begun && ps_playing() && tk >= 4u && (int)((tk - 4u) / (4u * PS_TPB)) == au.bar && bpm > 1.0f)
        m += (float)((tk - 4u) % (4u * PS_TPB)) / (float)PS_TPB / bpm;
    return all ? au.total + m : m;
}
/* for the rule: the set's time so far (12:34, past an hour 1:02:34), the same for every length; waiting or ending,
 * a word with AUTO */
static const char *au_text(void)
{
    static char t[32];
    uint32_t n, sec;
    char v[12];
    if (!ps_playing())
        return "AUTO: PLAY";
    if (au.closing)
        return "AUTO: THE END";
    sec = (uint32_t)(au_elapsed(1) * 60.0f);
    n = 0;
    if (sec >= 3600u) {
        int_text((int)(sec / 3600u), v);
        n += str_put(t + n, sizeof t - n, v);
        t[n++] = ':';
        n += au_put2(t + n, sec / 60u % 60u);
    } else {
        int_text((int)(sec / 60u), v);
        n += str_put(t + n, sizeof t - n, v);
    }
    t[n++] = ':';
    au_put2(t + n, sec % 60u);
    return t;
}
/* for the rule's bar: how far into the set (an endless one: into its hour), 0..1; the chapters' ends in *ends (n of
 * them, the last not: the set's own end) */
static float au_progress(float *ends, int *n)
{
    static const uint8_t MIN[AU_NLEN] = {30, 60, 90, 60};
    float len = (float)MIN[au.len];
    uint32_t i;
    *n = 0;
    for (i = 0; i + 1u < au.nch && i < AU_NCH; i++)
        ends[(*n)++] = fm_clampf(au.ch[i].end / len, 0.0f, 1.0f);
    return au.closing ? 1.0f : fm_clampf(au_elapsed(0) / len, 0.0f, 1.0f);
}

/* the next chapter's world and key handed to the engine, which takes them on the drop, whenever it comes (the set's or
 * the hand's): from the last break on, anew each bar (the key may have become the hand's) */
static void au_next(int sec)
{
    int i;
    if (!au.staged) {
        if (au.cur + 1u < au.nch) {
            au.staged = 1;
        } else if (sec == SEC_BUILD) {   /* (endless: the next hour, planned as the build begins) */
            au.total += au.min;
            au.min = 0.0f;
            au.cycle++;
            au_plan(2, ps_world_now(), ps_root(), au.bpm);
            au_chapter(0);
            au.staged = 2;
        } else {
            return;
        }
    }
    i = au.staged == 2u ? 0 : au.cur + 1;
    ps_stage(au.ch[i].world, au_held(P_ROOT) ? ps_root() : au.ch[i].root);
}

static void au_bar(int sec)              /* a bar has begun */
{
    const au_ch_t *c = &au.ch[au.cur];
    int k, rb = au.bar - au.drop_bar, at;
    float u;
    if (sec != au.sec) {                 /* a section begins */
        if (sec == SEC_BREAK) {
            au.ours = (uint8_t)(au.ours == 2u);   /* (2: cued by the set a bar ago) */
            if (au.ours && au.kind == K_MAIN && !au.main_done) {
                au.main_done = 1;
                au.min_main = au.min;
            }
        }
        if (au.kind == K_B && au.cued && au.cur + 1u < au.nch) {   /* B: the next chapter's, from its break (or a build
                                                                   * called by hand on its bar: the world came anyway) */
            au_chapter(au.cur + 1);
            hd_chapter();
            c = &au.ch[au.cur];
        }
        au.sec = sec;
        au.sec_at = au.bar;
        au.cued = 0;
    }
    at = au.bar - au.sec_at;             /* bars into it */
    if (ps_ui.drops != au.drops) {       /* a drop */
        au.drops = ps_ui.drops;
        au.drop_bar = au.bar;
        rb = 0;
        au.ours = 0;
        if (au.staged || (au.kind == K_B && au.cued && au.cur + 1u < au.nch)) {   /* (B's cue met by a hand's drop:
                                                                                  * its world came with it) */
            au_chapter(au.staged == 2u ? 0 : au.cur + 1);
            hd_chapter();
            c = &au.ch[au.cur];
        }
        au_segment();
    }
    /* the tempo along the chapter, a tenth a bar at most; the macros a step a bar */
    u = c->end > au.start ? fm_clampf((au.min - au.start) / (c->end - au.start), 0.0f, 1.0f) : 1.0f;
    if (!au_held(P_TEMPO)) {
        float d = au.from + (c->bpm - au.from) * u - au.bpm;
        au.bpm += fm_clampf(d, -0.1f, 0.1f);
    }
    for (k = 0; k < 4; k++)
        if (!au_held(P_ENERGY + k)) {
            float d = au_goal(k) - au.val[k];
            au.val[k] += fm_clampf(d, -1.0f, 1.0f);
        }
    if (!au_held(P_BUILD_LEN) && sec != SEC_BUILD)
        ps_set(P_BUILD_LEN, au.kind == K_MAIN || au.kind == K_B ? 2 : 1);   /* (a full break's: 8 bars) */
    /* the sections */
    if (au.closing) {                    /* the set's last break: it fades; a build called by hand: the set is over */
        if (sec != SEC_BREAK) {
            au_stop(1);
            app_say("AUTO", "OFF");
            return;
        }
    } else if (sec == SEC_MAIN) {
        int g = au.groove;
        if (au.intro) {                  /* the set's beginning: its line and its events come in by degrees */
            hd_intro(HD_BAR * (uint32_t)au.bar, c->high < 50 ? 32 : 16, 0);
            au.intro = 0;
        }
        if (au_held(P_BREAKS) || au.cued) {
        } else if (au.kind == K_ROLL || au.kind == K_C) {   /* a drop of its own, or the next chapter's */
            if (!au.edged && rb + (au.kind == K_C ? 17 : 9) >= g) {   /* (its edge: planned a bar ahead) */
                au.edged = 1;
                hd_edge(au.kind == K_C ? -1 : au.brk, HD_BAR * (uint32_t)(au.drop_bar + g));
            }
            if (rb + 1 >= g) {
                uint32_t e = HD_BAR * (uint32_t)(au.bar + 1);
                au.cued = 1;
                if (au.kind == K_C && au_more())
                    au_next(sec);        /* (staged with the cue: a hand's drop before it is no chapter's) */
                ps_cue(PS_CUE_DROP);
                if (au.staged)
                    hd_intro(e, au.ch[au.staged == 2u ? 0 : au.cur + 1].high < 50 ? 32 : 16, PS_AG_DROP);
                else if (set.auto_perf)
                    hd_drop(0);
            }
        } else if (rb + 1 >= g && (rb + 1) % 8 == 0) {
            au.cued = 1;
            if (au.kind == K_BUILD) {
                ps_cue(PS_CUE_BUILD);
            } else {
                ps_cue(PS_CUE_BREAK);
                au.ours = 2;
                if (au.kind == K_B && au.cur + 1u < au.nch)   /* (the next world and key as the break begins) */
                    ps_stage_bar(au.ch[au.cur + 1u].world, au_held(P_ROOT) ? ps_root() : au.ch[au.cur + 1u].root);
            }
        }
    } else if (sec == SEC_BREAK) {
        if (au.ours && au.last && !au_more()) {   /* the end */
            au.closing = 1;              /* (what its hands carried into it goes on; nothing new) */
        } else if (!au.cued && at + 1 >= (au.ours ? au.brk : AU_HOLD)) {   /* (one called by hand: the hand's build, or in time) */
            if (au.ours && au.kind == K_SHORT) {   /* a short break: the drop at once */
                au.cued = 1;
                ps_cue(PS_CUE_DROP);
                if (set.auto_perf)
                    hd_drop(0);
            } else {
                ps_cue(PS_CUE_BUILD);
            }
        }
    }
    if (!au.closing && sec != SEC_MAIN && (au.last || au.staged) && au_more())
        au_next(sec);                    /* (staged: an endless hour's next, its chapter 0 begun; the key re-staged) */
    au_push();
    if (!au.closing)
        hd_bar(sec, at);
#ifdef PLAT_HOST
    if (host_auto_log)
        printf("AU %d min %.2f ch %d/%d world %d bpm %.3f set %.3f root %d sec %d rb %d mac %d %d %d %d last %d kind %d seg %d/%d\n",
               au.bar, (double)(au.total + au.min), au.cur, au.nch, ps_world_now(), (double)ps_bpm(), (double)au.bpm, ps_root(),
               sec, au.bar - au.drop_bar, ps_get(0), ps_get(1), ps_get(2), ps_get(3), au.last, au.kind, au.seg, au.nseg);
#endif
}

static void au_tick(void)                /* each frame */
{
    int bar, sec;
    if (au_rested && (int32_t)(plat_ms() - au_rested) > (int32_t)(ps_playing() ? 300u : 3000u)) {   /* (the echoes gone; or PLAY) */
        au_rested = 0;
        au_fade = 1.0f;
    }
    if (!au.on && !au_rested && au_fade < 1.0f)   /* (the last break left by hand: back in a second or so) */
        au_fade = fm_clampf(au_fade + 0.015f, 0.0f, 1.0f);
#ifdef PLAT_HOST
    if (host_auto_log) {
        static int st[4];
        int now[4] = {ps_stat(9), ps_stat(10), ps_stat(11), ps_stat(12)}, k, ch = 0;
        for (k = 0; k < 4; k++) {
            ch |= now[k] != st[k];
            st[k] = now[k];
        }
        if (ch)
            printf("ST %u sec %d fx %d out %d half %d throws %d\n", (unsigned)ps_ui.tick, ps_section(), st[0], st[1], st[2], st[3]);
    }
#endif
    if (!au.on)
        return;
    if (!ps_playing()) {                 /* resting: the set waits at its beginning */
        if (au.begun)
            au_top();
        return;
    }
    bar = au_bar_now();
    if (au.begun && bar < au.bar)        /* from the top again */
        au_top();
    if (au.closing) {
        float f = 1.0f - ((float)ps_ui.tick / (4.0f * PS_TPB) - (float)au.sec_at - 8.0f) / (float)AU_FADE_BARS;
        au_fade = fm_clampf(f, 0.0f, 1.0f);
        if (f <= 0.0f) {
            ps_play(0);
            au_rested = plat_ms() | 1u;
            au_stop(0);
            app_say("AUTO", "THE SET IS OVER");
            return;
        }
    }
    if (au.begun && bar == au.bar)
        return;
    sec = ps_section();
    if (au.begun) {                      /* (a MIDI clock's tempo, if it leads) */
        float b = (float)ps_bpm();
        au.min += (float)(bar - au.bar) * 4.0f / (b > au.bpm + 1.0f || b < au.bpm - 1.0f ? b : au.bpm);
    } else {                             /* (the set's first bar: PLAY's beginning is no drop of its own) */
        au.drops = ps_ui.drops;
    }
    au.begun = 1;
    au.bar = bar;
    au_bar(sec);
}
