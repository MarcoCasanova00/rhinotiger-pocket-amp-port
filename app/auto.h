// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 hacha
/* AUTO PLAY: a set, played as a DJ would: 30, 60 or 90 minutes, or without end. Included in app.c; on the UI's side, it
 * only turns what a hand could (psy.h), a bar at a time.
 *
 * A set is drawn from its number: a curve of how high it stands (one of several, none chosen by hand), and chapters
 * of some 5 to 9 minutes along it: a world, a tempo reached by its end, a key, the macros, how dark. Within a chapter
 * the groove runs 48 to 96 bars, then a break, a build, the drop; the last drop of a chapter is the next one's first
 * bar: its world and its key come there, never elsewhere. The tempo glides, a tenth of a BPM a bar at most, and goes
 * on through the change of world; the macros move one step a bar.
 *
 * What a hand turns is the hand's for AU_HOLD bars, then glides back (the key: at the next chapter; a world: for its
 * chapter). A break or a build called by hand is followed. The settings kept (set) are the hand's alone: SAVE during a
 * set keeps those. Off: the music goes on from where it is, the knobs where the set left them. A set done: its last
 * break fades, the music rests, and everything is the hand's again.
 *
 * Its dice are its own (never the engine's): the same number draws the same set; what the generator plays within it is
 * its own each time. */
#define AU_HOLD 32                       /* bars a thing touched stays the hand's */
#define AU_NCH 20
#define AU_FADE_BARS 12
enum { AU_LEN_30, AU_LEN_60, AU_LEN_90, AU_LEN_ENDLESS, AU_NLEN };
typedef struct {
    uint8_t world, root;
    uint8_t mac[4];                      /* P_ENERGY .. P_DRIFT */
    uint8_t high;                        /* how high it stands, 0..100 */
    float bpm;                           /* reached by its end */
    float end;                           /* minutes from the set's start (endless: of its cycle) */
} au_ch_t;
static struct {
    uint8_t on, len, nset, cycle;
    uint8_t nch, cur, last;              /* the chapters, the one now; last: this groove is its chapter's last */
    uint8_t begun;                       /* bars have been counted (the music plays) */
    uint8_t ours, staged, closing;       /* the break now is the set's; the next world waits for its drop; the fade */
    uint32_t rng;
    au_ch_t ch[AU_NCH];
    float from;                          /* the tempo as the chapter began */
    float bpm, start, min;               /* now; the chapter's start; since the set's (its cycle's) start */
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
 * 2, it goes on from them into another */
static void au_plan(int go_on, int world, int root, float bpm)
{
    static const uint8_t MIN[AU_NLEN] = {30, 60, 90, 60};
    float len = (float)MIN[au.len], sum = 0.0f, w[AU_NCH], bot, top, d0, d1, d2, drift, t = 0.0f;
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
        if (i || go_on)                  /* a chapter never moves it far, and down less than up */
            want = fm_clampf(want, bpm - 8.0f, bpm + 14.0f);
        want = fm_clampf(want, 140.0f, 190.0f);
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
#ifdef PLAT_HOST
    if (host_auto_log)
        for (i = 0; i < au.nch; i++)
            printf("PLAN %d ch %d/%d curve %d world %d root %d bpm %.1f high %d end %.1f mac %d %d %d %d\n", au.nset, (int)i, au.nch,
                   (int)curve, au.ch[i].world, au.ch[i].root, (double)au.ch[i].bpm, au.ch[i].high, (double)au.ch[i].end,
                   au.ch[i].mac[0], au.ch[i].mac[1], au.ch[i].mac[2], au.ch[i].mac[3]);
#endif
}

static float au_goal(int k)              /* macro k's place now: its chapter's, rising a little along it */
{
    const au_ch_t *c = &au.ch[au.cur];
    float u = c->end > au.start ? fm_clampf((au.min - au.start) / (c->end - au.start), 0.0f, 1.0f) : 1.0f;
    return fm_clampf((float)c->mac[k] + (k < 2 ? (u - 0.5f) * 12.0f : 0.0f), 0.0f, 100.0f);
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
static void au_segment(void)             /* a groove begins: how long, and whether its drop is the next chapter's */
{
    static const uint8_t G[4] = {48, 64, 64, 96};
    static const uint8_t B[4] = {8, 16, 16, 24};
    const au_ch_t *c = &au.ch[au.cur];
    float left = (c->end - au.min) * au.bpm / 4.0f;   /* bars */
    au.brk = B[au_rnd(4u)];
    au.groove = G[au_rnd(4u)];
    au.last = left < (float)(au.groove + au.brk) + 60.0f;   /* (no room for another after it) */
    if (au.last) {
        if (au.brk < 16)
            au.brk = 16;
        au.groove = clamp(((int)left - au.brk - 8 + 4) / 8 * 8, 32, 128);
    }
}
static void au_chapter(int i)            /* chapter i begins, on its drop */
{
    au.cur = (uint8_t)i;
    au.start = au.min;
    au.from = au.bpm;
    au.staged = 0;
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
    au.bpm = au.ch[0].bpm - (float)au_rnd(3u);
    au_chapter(0);
    for (k = 0; k < 4; k++)
        au.val[k] = (float)au.ch[0].mac[k] - (k < 2 ? 6.0f : 0.0f);
    for (k = 0; k < P_NPARAMS; k++)
        au.hold[k] = 0;
    au.bar = au.drop_bar = 0;
    au.sec = SEC_MAIN;
    au.drops = ps_ui.drops;
    au.begun = 0;
    ps_world(au.ch[0].world);
    ps_auto_root(au.ch[0].root);
    au_push();
    au_segment();
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
    if (!ps_playing() || boot_hold) {
        au_top();
        return;
    }
    au.cycle = 0;                        /* playing: from where the music is, its first chapter */
    au.total = au.min = 0.0f;
    au.closing = au.staged = au.ours = 0;
    au.bpm = (float)ps_bpm();
    au_plan(1, ps_world_now(), ps_root(), au.bpm);
    au_chapter(0);
    for (k = 0; k < 4u; k++)
        au.val[k] = (float)ps_get(P_ENERGY + (int)k);
    for (k = 0; k < P_NPARAMS; k++)
        au.hold[k] = 0;
    au.bar = au_bar_now();
    au.drop_bar = au.bar / 8 * 8;
    au.sec = ps_section();
    au.sec_at = au.bar;
    au.drops = ps_ui.drops;
    au.begun = 1;
    ps_auto_root(ps_root());
    au_push();
    au_segment();
}
static void au_stop(int keep)            /* off: keep, the music goes on, the knobs where they stand; else the hand's */
{
    int k;
    if (!au.on)
        return;
    au.on = 0;
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

static const char *au_text(void)         /* for the rule: its chapter, its minutes */
{
    static char t[32];
    uint32_t n;
    char v[12];
    if (!ps_playing())
        return "AUTO: PLAY";
    if (au.closing)
        return "AUTO: THE END";
    n = str_put(t, sizeof t, "AUTO ");
    int_text((int)(au.total + au.min), v);
    n += str_put(t + n, sizeof t - n, v);
    if (au.len == AU_LEN_ENDLESS)
        str_put(t + n, sizeof t - n, " MIN");
    else
        str_put(t + n, sizeof t - n, au.len == AU_LEN_30 ? "/30" : au.len == AU_LEN_60 ? "/60" : "/90");
    return t;
}

static void au_bar(int sec)              /* a bar has begun */
{
    const au_ch_t *c = &au.ch[au.cur];
    int k, rb = au.bar - au.drop_bar, at;
    float u;
    if (sec != au.sec) {                 /* a section begins */
        if (sec == SEC_BREAK)
            au.ours = (uint8_t)(au.ours == 2u);   /* (2: cued by the set a bar ago) */
        au.sec = sec;
        au.sec_at = au.bar;
    }
    at = au.bar - au.sec_at;             /* bars into it */
    if (ps_ui.drops != au.drops) {       /* a drop */
        au.drops = ps_ui.drops;
        au.drop_bar = au.bar;
        rb = 0;
        au.ours = 0;
        if (au.staged) {
            au_chapter(au.staged == 2u ? 0 : au.cur + 1);
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
        ps_set(P_BUILD_LEN, au.last ? 2 : 1);   /* (a chapter's last: 8 bars) */
    /* the sections */
    if (au.closing) {                    /* the set's last break: it fades; a build called by hand: the set is over */
        if (sec != SEC_BREAK) {
            au_stop(1);
            app_say("AUTO", "OFF");
            return;
        }
    } else if (sec == SEC_MAIN) {
        if (!au_held(P_BREAKS) && rb + 1 >= au.groove && (rb + 1) % 8 == 0) {
            ps_cue(PS_CUE_BREAK);
            au.ours = 2;
        }
    } else if (sec == SEC_BREAK) {
        if (au.ours && au.last && !au_more()) {   /* the end */
            au.closing = 1;
        } else if (at + 1 >= (au.ours ? au.brk : AU_HOLD))   /* (one called by hand: the hand's build, or in time) */
            ps_cue(PS_CUE_BUILD);
    } else if (au.last && au_more() && ps_ui.sec_bar + 1 >= ps_ui.sec_len && !au.staged) {   /* a build's last bar */
        int root;
        if (au.cur + 1u < au.nch) {
            root = au_held(P_ROOT) ? ps_root() : au.ch[au.cur + 1u].root;
            ps_stage(au.ch[au.cur + 1u].world, root);
            au.staged = 1;
        } else {                         /* (endless: the next hour) */
            au.total += au.min;
            au.min = 0.0f;
            au.cycle++;
            au_plan(2, ps_world_now(), ps_root(), au.bpm);
            au_chapter(0);
            root = au_held(P_ROOT) ? ps_root() : au.ch[0].root;
            ps_stage(au.ch[0].world, root);
            au.staged = 2;
        }
        ps_kit(2);
    }
    au_push();
#ifdef PLAT_HOST
    if (host_auto_log)
        printf("AU %d min %.2f ch %d/%d world %d bpm %.3f set %.3f root %d sec %d rb %d mac %d %d %d %d last %d\n", au.bar,
               (double)(au.total + au.min), au.cur, au.nch, ps_world_now(), (double)ps_bpm(), (double)au.bpm, ps_root(), sec,
               au.bar - au.drop_bar, ps_get(0), ps_get(1), ps_get(2), ps_get(3), au.last);
#endif
}

static void au_tick(void)                /* each frame */
{
    int bar, sec;
    if (au_rested && plat_ms() - au_rested > (ps_playing() ? 300u : 3000u)) {   /* (the echoes gone; or PLAY) */
        au_rested = 0;
        au_fade = 1.0f;
    }
    if (!au.on && !au_rested && au_fade < 1.0f)   /* (the last break left by hand: back in a second or so) */
        au_fade = fm_clampf(au_fade + 0.015f, 0.0f, 1.0f);
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
    }
    au.begun = 1;
    au.bar = bar;
    au_bar(sec);
}
