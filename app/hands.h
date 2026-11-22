// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 hacha
/* AUTO PLAY's hands: the effects, the parts taken out, the keys. Included in auto.h (its au). Planned a bar or more
 * ahead, as ps_auto_do's things from a tick to a tick (the engine keeps them on the grid and ends them; a section's
 * change ends all but those meant to go on).
 *
 * In a groove: at a phrase's end (8 bars) a move now and then, at the 16s and 32s more often and larger; a part left out
 * for a phrase or two, back on a phrase's first beat; a throw now and then on an offbeat, a run of them into a phrase's
 * end; the chapter's motif (one or a few cells of a sound of the chapter's, along the scale, the set's own to come back
 * to) over 16 bars or 32. In a break: one or two of the long effects, few throws, the motif again, softer. In a build:
 * none (it has its own). On a drop: a throw on its first beat, a part held back.
 *
 * PERFORM (0..100) is how much of it. The hand's own take over: a grid's effect (or a fill, a gap, half time, the kick
 * and the bass out) played: no effects and no parts out for AU_HOLD bars; a part muted or back: that part left alone so
 * long; a key thrown: no throws so long, and none while the keys' loop (REC) is there. */
static struct {
    uint32_t rng;
    int hold_fx, hold_keys, hold_part[NPART];   /* till this bar the hand's */
    int out_to, mot_to;                  /* a part out, the motif: till this bar */
    uint8_t hist[3], nhist;              /* the latest moves, not again at once */
    uint8_t lead, acc[2], low;           /* the chapter's sounds (white keys', 0..15) */
    uint8_t mlen;                        /* its motif: bars, */
    uint8_t mn;
    struct {
        uint8_t pos, snd;                /* (pos: a sixteenth of it) */
        int8_t semi;
        uint8_t bite;
    } m[PS_MOTIF_N];
} hd;
enum { MV_ROLL8, MV_ROLL8T, MV_ROLLUP, MV_REV, MV_GATE, MV_HIGH, MV_LOW, MV_STAIRS, MV_TAPE, MV_BAND, MV_GAP, MV_KICK,
       MV_KB, MV_BASS, MV_HALF, MV_FILL, MV_DUB, MV_WASH, MV_FREEZE, MV_N };
#ifdef PLAT_HOST
static const char *const MV_NAME[MV_N] = {"ROLL8", "ROLL8T", "ROLLUP", "REV", "GATE", "HIGH", "LOW", "STAIRS", "TAPE",
                                          "BAND", "GAP", "KICK", "KB", "BASS", "HALF", "FILL", "DUB", "WASH", "FREEZE"};
#define HD_LOG(...) do { if (host_auto_log) printf(__VA_ARGS__); } while (0)
#else
#define HD_LOG(...) do { } while (0)
#endif
enum { HF_R4, HF_R4T, HF_R8, HF_R8T, HF_LOW, HF_HIGH, HF_FLANGE, HF_BAND, HF_WASH, HF_DUB, HF_FREEZE, HF_REV, HF_TAPE,
       HF_RISER, HF_STAIRS, HF_GATE };   /* (the grid's order) */
#define HD_BAR 96u
#define HD_FX_KINDS (1u << PS_AG_FX | 1u << PS_AG_OUT | 1u << PS_AG_HALF | 1u << PS_AG_FILL | 1u << PS_AG_GAP)
#define HD_KEY_KINDS (1u << PS_AG_THROW | 1u << PS_AG_MOTIF)

static uint32_t hd_rnd(uint32_t n)
{
    hd.rng ^= hd.rng << 13;
    hd.rng ^= hd.rng >> 17;
    hd.rng ^= hd.rng << 5;
    return (hd.rng >> 8) % n;
}
static int hd_ch(float p)                /* a chance, by PERFORM and how high the chapter stands */
{
    float a = (float)set.auto_perf / 50.0f * (0.7f + 0.6f * (float)au.ch[au.cur].high / 100.0f);
    return (float)hd_rnd(1000u) < 1000.0f * fm_minf(p * a, 0.95f);
}
static void hd_do(int kind, int id, uint32_t t0, uint32_t t1, int flag)
{
    ps_ag_t g = {(uint8_t)kind, (uint8_t)id, (uint8_t)flag, 0, 0, t0, t1};
    ps_auto_do(&g);
}
static void hd_throw(int snd, int semi, int bite, uint32_t t, int flag)
{
    ps_ag_t g = {PS_AG_THROW, (uint8_t)snd, (uint8_t)flag, (uint8_t)bite, (int8_t)semi, t, t + 1u};
    ps_auto_do(&g);
}
static int hd_fx_ok(void) { return au.bar >= hd.hold_fx && !pf.fx_held; }
static int hd_keys_ok(void) { return au.bar >= hd.hold_keys && !ps_take() && !ps_rec_state(); }
static int hd_part_ok(int p) { return hd_fx_ok() && au.bar >= hd.hold_part[p] && !ps_ui.muted; }

static void hd_clear(uint32_t kinds) { ps_auto_clear(kinds); }
/* the hand's own (app.c): an effect or the like; a part muted or back; a key thrown, REC */
static void hd_hand_fx(void)
{
    if (!au.on)
        return;
    hd.hold_fx = au.bar + AU_HOLD;
    hd.out_to = 0;
    hd_clear(HD_FX_KINDS);
}
static void hd_hand_part(int p)
{
    if (!au.on)
        return;
    hd.hold_part[p] = au.bar + AU_HOLD;
    ps_auto_back(p);
}
static void hd_hand_key(void)
{
    if (!au.on)
        return;
    hd.hold_keys = hd.hold_part[PART_EVENTS] = au.bar + AU_HOLD;   /* (heard: the events not out) */
    ps_auto_back(PART_EVENTS);
    hd.mot_to = 0;
    hd_clear(HD_KEY_KINDS);
}

static int hd_scale(int d)               /* a degree of the scale, in semitones (Phrygian, as the keys' rhythms) */
{
    static const int8_t PH[7] = {0, 1, 3, 5, 7, 8, 10};
    int o = d >= 0 ? d / 7 : -((-d + 6) / 7);
    return 12 * o + PH[d - 7 * o];
}
static int hd_pitch(int how, int i, int *d)   /* a cell's i-th pitch: flat, climbing, falling, wandering, leaping, anywhere */
{
    static const int8_t LEAP[6] = {0, 0, 7, 12, -5, 12};
    switch (how) {
    case 0: return 0;
    case 1: return 3 * i;
    case 2: return -3 * i;
    case 3: *d += (int)hd_rnd(5u) - 2; return hd_scale(clamp(*d, -3, 8));
    case 4: return LEAP[hd_rnd(6u)];
    default: return hd_scale((int)hd_rnd(8u));
    }
}

static void hd_chapter(void)             /* a chapter's sounds and its motif */
{
    static const uint8_t LEAD[9] = {3, 5, 6, 7, 8, 9, 11, 13, 14}, ACC[10] = {5, 7, 8, 9, 10, 11, 12, 13, 14, 15}, LOW[4] = {0, 1, 2, 4};
    static const uint8_t AT[8] = {2, 3, 6, 7, 10, 11, 12, 14};
    uint32_t cells, c, i, half;
    hd.lead = LEAD[hd_rnd(9u)];
    hd.acc[0] = ACC[hd_rnd(10u)];
    hd.acc[1] = ACC[hd_rnd(10u)];
    hd.low = LOW[hd_rnd(4u)];
    hd.mlen = (uint8_t)(hd_rnd(10u) < 6u ? 2 : 4);
    hd.mn = 0;
    cells = 2u + hd_rnd(hd.mlen == 4 ? 3u : 2u);
    for (c = 0; c < cells; c++) {
        uint32_t n = 2u + hd_rnd(4u), gap = hd_rnd(5u) < 3u ? 1u : 2u + hd_rnd(2u), pos, how = hd_rnd(6u), snd = hd_rnd(4u) ? hd.lead : hd.acc[0];
        int d = 0;
        half = 16u * hd.mlen / 2u;       /* (the first cell in its first half, the rest mostly in the answering half) */
        pos = (c == 0 ? 0u : hd_rnd(4u) ? half : 0u) + 16u * hd_rnd(half / 16u) + AT[hd_rnd(8u)];
        for (i = 0; i < n && hd.mn < PS_MOTIF_N; i++, pos += gap) {
            uint32_t k, taken = 0;
            int semi = hd_pitch((int)how, (int)i, &d);
            if (pos >= 16u * hd.mlen)
                break;
            for (k = 0; k < hd.mn; k++)
                taken |= hd.m[k].pos == pos;
            if (taken)
                continue;
            hd.m[hd.mn].pos = (uint8_t)pos;
            hd.m[hd.mn].snd = (uint8_t)snd;
            hd.m[hd.mn].semi = (int8_t)clamp(semi, -12, 15);
            hd.m[hd.mn].bite = (uint8_t)(15u + hd_rnd(35u));
            hd.mn++;
        }
    }
}
static void hd_begin(void)               /* a set begins */
{
    uint32_t k;
    hd.rng = (0x9E3779B9u * (au.nset + 1u)) ^ (0xB5297A4Du * (au.len + 1u)) ^ 0x68E31DA4u;
    for (k = 0; k < 8u; k++)
        hd_rnd(2u);
    hd.hold_fx = hd.hold_keys = hd.out_to = hd.mot_to = 0;
    for (k = 0; k < NPART; k++)
        hd.hold_part[k] = 0;
    hd.nhist = 0;
    hd_clear(255u);
    hd_chapter();
}

static void hd_motif(uint32_t t0, int bars, int soft)   /* the chapter's motif (each time a note or two of it other, for that time) */
{
    int id;
    uint32_t k;
    ps_motif_t *mo;
    if (!hd.mn)
        return;
    mo = ps_auto_motif(&id);
    mo->n = 0;
    mo->len = (uint16_t)(HD_BAR * hd.mlen);
    for (k = 0; k < hd.mn; k++) {
        if (soft && (k & 1u))            /* (a break: half of it) */
            continue;
        mo->e[mo->n].pos = (uint16_t)(6u * hd.m[k].pos);
        mo->e[mo->n].snd = hd.m[k].snd;
        mo->e[mo->n].semi = hd.m[k].semi;
        mo->e[mo->n].bite = (uint8_t)(soft ? hd.m[k].bite / 3u : hd.m[k].bite);
        mo->n++;
    }
    for (k = mo->n ? hd_rnd(3u) : 0u; k; k--)
        mo->e[hd_rnd(mo->n)].semi = (int8_t)hd_scale((int)hd_rnd(8u));
    hd_do(PS_AG_MOTIF, id, t0, t0 + HD_BAR * (uint32_t)bars, 0);
    hd.mot_to = (int)(t0 / HD_BAR) + bars;
    HD_LOG("HD motif bar %d bars %d notes %d\n", (int)(t0 / HD_BAR), bars, mo->n);
}

static void hd_run(uint32_t e)           /* a few throws into the edge at tick e: a beat of sixteenths, or four 32nds */
{
    uint32_t fast = hd_rnd(4u) == 0u, n = fast ? 4u : 2u + hd_rnd(3u), gap = fast ? 3u : 6u, i, how = hd_rnd(6u);
    int d = 0, snd = hd_rnd(2u) ? hd.lead : hd.acc[hd_rnd(2u)];
    for (i = 0; i < n; i++)
        hd_throw(snd, clamp(hd_pitch((int)how, (int)i, &d), -12, 15), 30 + (int)hd_rnd(40u), e - gap * (n - i), 0);
    HD_LOG("HD run tick %u n %d\n", (unsigned)e, (int)n);
}

/* a move into the edge at tick e: size 0 (a phrase's), 1 (16 bars'), 2 (32's, or a break's: last). Returns 1 if it
 * covers the last beat */
static int hd_move(uint32_t e, int size, int last)
{
    /* how likely each, by the size; a break's */
    static const uint8_t WT[4][MV_N] = {
        /*  R8 R8T UP REV GATE HI LOW STAIR TAPE BAND GAP KICK KB BASS HALF FILL DUB WASH FRZ */
        {12, 6, 0, 8, 0, 0, 0, 2, 0, 3, 5, 4, 3, 0, 0, 6, 4, 0, 0},
        {6, 5, 8, 6, 5, 7, 0, 5, 1, 4, 5, 8, 6, 5, 5, 2, 3, 0, 0},
        {2, 2, 8, 3, 5, 12, 8, 6, 4, 3, 5, 6, 8, 6, 11, 0, 2, 0, 0},
        {0, 2, 6, 3, 4, 12, 8, 5, 6, 3, 2, 3, 6, 0, 0, 0, 10, 8, 5},
    };
    const uint8_t *wt = WT[last ? 3 : size];
    uint32_t sum = 0, r, k, mv;
    uint8_t w[MV_N];
    for (k = 0; k < MV_N; k++) {
        w[k] = wt[k];
        for (r = 0; r < hd.nhist; r++)
            if (hd.hist[r] == k)
                w[k] = 0;
        if ((k == MV_KICK && !hd_part_ok(PART_KICK)) || (k == MV_BASS && !hd_part_ok(PART_BASS)) ||
            (k == MV_KB && !(hd_part_ok(PART_KICK) && hd_part_ok(PART_BASS))) || (k == MV_WASH && ps_shedding()))
            w[k] = 0;
        sum += w[k];
    }
    if (!sum)
        return 0;
    r = hd_rnd(sum);
    for (mv = 0; mv < MV_N - 1u && r >= w[mv]; mv++)
        r -= w[mv];
    hd.hist[2] = hd.hist[1];
    hd.hist[1] = hd.hist[0];
    hd.hist[0] = (uint8_t)mv;
    if (hd.nhist < 3u)
        hd.nhist++;
    HD_LOG("HD move %s bar %d size %d last %d\n", MV_NAME[mv], (int)(e / HD_BAR), size, last);
    switch (mv) {
    case MV_ROLL8: hd_do(PS_AG_FX, HF_R8, e - 24u, e, 0); return 1;
    case MV_ROLL8T: hd_do(PS_AG_FX, HF_R8T, e - 24u, e, 0); return 1;
    case MV_ROLLUP:
        hd_do(PS_AG_FX, HF_R4, e - 96u, e - 48u, 0);
        hd_do(PS_AG_FX, HF_R8, e - 48u, e - 24u, 0);
        hd_do(PS_AG_FX, HF_R8T, e - 24u, e, 0);
        return 1;
    case MV_REV: hd_do(PS_AG_FX, HF_REV, e - 24u, e, 0); return 1;
    case MV_GATE: hd_do(PS_AG_FX, HF_GATE, e - 96u, e, 0); return 0;
    case MV_HIGH: hd_do(PS_AG_FX, HF_HIGH, e - (size == 2 || last ? 192u : 96u), e, 0); return 0;
    case MV_LOW: hd_do(PS_AG_FX, HF_LOW, e - 192u, e, 0); return 0;
    case MV_STAIRS: hd_do(PS_AG_FX, HF_STAIRS, e - (size ? 48u : 24u), e, 0); return 1;
    case MV_TAPE: hd_do(PS_AG_FX, HF_TAPE, e - 48u, e, 0); return 1;
    case MV_BAND: hd_do(PS_AG_FX, HF_BAND, e - (size ? 96u : 48u), e, 0); return 0;
    case MV_GAP: hd_do(PS_AG_GAP, 0, e - 24u, e, 0); return 1;
    case MV_KICK: hd_do(PS_AG_OUT, 1 << PART_KICK, e - (size ? 96u : 48u), e, 0); return 0;
    case MV_KB: hd_do(PS_AG_OUT, 1 << PART_KICK | 1 << PART_BASS, e - (size == 2 ? 96u : size ? 48u : 24u), e, 0); return 0;
    case MV_BASS: hd_do(PS_AG_OUT, 1 << PART_BASS, e - (size == 2 ? 192u : 96u), e, 0); return 0;
    case MV_HALF: hd_do(PS_AG_HALF, 0, e - (size == 2 ? 384u : 192u), e, 0); return 0;
    case MV_FILL: hd_do(PS_AG_FILL, 0, e - 24u, e, 0); return 0;
    case MV_DUB: hd_do(PS_AG_FX, HF_DUB, e - (last ? 24u : 48u), last ? e + 192u : e, last ? PS_AG_BREAK : 0); return 0;
    case MV_WASH: hd_do(PS_AG_FX, HF_WASH, e - 24u, e + 192u, PS_AG_BREAK); return 0;
    case MV_FREEZE: hd_do(PS_AG_FX, HF_FREEZE, e - 24u, e + 192u, PS_AG_BREAK); return 0;
    }
    return 0;
}

static void hd_strip(uint32_t t0, int left, int flag, int thrown)   /* a part left out from t0, left bars of the groove
                                                                     * from there (thrown: not the events, a throw on it) */
{
    static const uint8_t PART[6] = {PART_LINE, PART_LINE, PART_EVENTS, PART_EVENTS, PART_PERC, PART_ATMOS};
    int p = PART[hd_rnd(6u)], bars = p == PART_PERC ? 4 : hd_rnd(2u) ? 16 : 8;
    if (bars > left - 8)
        bars = left - 8;
    if (bars < 4 || !hd_part_ok(p) || (p == PART_EVENTS && (hd.mot_to > (int)(t0 / HD_BAR) || thrown || ps_take() || ps_rec_state())))
        return;
    hd_do(PS_AG_OUT, 1 << p, t0, t0 + HD_BAR * (uint32_t)bars, flag);
    hd.out_to = (int)(t0 / HD_BAR) + bars;
    HD_LOG("HD strip part %d bar %d bars %d\n", p, (int)(t0 / HD_BAR), bars);
}

static void hd_groove(int rb)            /* a groove's bar (rb from its drop): at a phrase's second bar, the phrase */
{
    int p = rb / 8, end = p * 8 + 8, last = end >= au.groove && !au_held(P_BREAKS), left = au.groove - end, size, i;
    uint32_t e = HD_BAR * (uint32_t)(au.drop_bar + end);
    if (rb % 8 != 1)
        return;
    size = end % 32 == 0 ? 2 : end % 16 == 0 ? 1 : 0;
    if (hd_fx_ok() && hd_ch(last ? 0.8f : size == 2 ? 0.75f : size == 1 ? 0.5f : 0.28f)) {
        if (!hd_move(e, size, last) && hd_keys_ok() && hd_ch(0.35f))
            hd_run(e);
    } else if (hd_keys_ok() && hd_ch(0.3f))
        hd_run(e);
    if (!last && hd.out_to <= au.drop_bar + end && hd_ch(0.2f))   /* the next phrase: a part out of it */
        hd_strip(e, left, 0, 0);
    if (!hd_keys_ok())
        return;
    if (!last && hd.mot_to <= au.drop_bar + end && hd.out_to <= au.drop_bar + end && left >= 8 && hd_ch(0.3f))   /* the motif, from the next */
        hd_motif(e, left >= 32 && hd_rnd(2u) ? 32 : left >= 16 ? 16 : 8, 0);
    else if (hd.mot_to <= au.bar)        /* a throw now and then, on an offbeat (now and then three, a dotted eighth apart) */
        for (i = 1; i < 6; i += 2)
            if (hd_ch(0.22f)) {
                static const uint8_t AT[6] = {2, 3, 6, 10, 11, 14};
                uint32_t t = HD_BAR * (uint32_t)(au.bar + i) + 6u * AT[hd_rnd(6u)], n = hd_rnd(4u) ? 1u : 3u, j;
                int snd = hd_rnd(5u) == 0u ? hd.low : hd.acc[hd_rnd(2u)], semi = hd_scale((int)hd_rnd(5u));
                for (j = 0; j < n; j++)
                    hd_throw(snd, semi, 35 - 10 * (int)j, t + 18u * j, 0);
                HD_LOG("HD throw tick %u n %d\n", (unsigned)t, (int)n);
            }
}

static void hd_break(int at)             /* a break's bar (at from its start) */
{
    int len = au.ours ? au.brk : 16;
    if (at == 1 && hd_fx_ok() && hd_ch(0.6f)) {   /* a long effect in its middle */
        static const uint8_t F[6] = {HF_WASH, HF_DUB, HF_FREEZE, HF_GATE, HF_BAND, HF_LOW};
        int f = F[hd_rnd(6u)], b = f == HF_FREEZE || f == HF_DUB ? 2 : 4;
        uint32_t t0 = HD_BAR * (uint32_t)(au.bar - 1 + len / 2);
        if (f != HF_WASH || !ps_shedding()) {
            hd_do(PS_AG_FX, f, t0, t0 + HD_BAR * (uint32_t)b, 0);
            HD_LOG("HD break fx %d bar %d bars %d\n", f, (int)(t0 / HD_BAR), b);
        }
    }
    if (!hd_keys_ok())
        return;
    if (at == 1 && len >= 16 && hd_ch(0.45f))   /* the motif again, half of it, softer, in the second half */
        hd_motif(HD_BAR * (uint32_t)(au.bar - 1 + len / 2), len / 2, 1);
    else if (at % 2 == 1 && hd.mot_to <= au.bar && hd_ch(0.25f)) {
        uint32_t t = HD_BAR * (uint32_t)(au.bar + 1) + 48u * hd_rnd(2u);
        hd_throw(hd_rnd(2u) ? hd.low : hd.lead, hd_scale((int)hd_rnd(5u)), 10, t, 0);
        HD_LOG("HD throw tick %u n 1\n", (unsigned)t);
    }
}

static void hd_drop(int chapter)         /* a build's last bar: the drop to come */
{
    uint32_t e = HD_BAR * (uint32_t)(au.bar + 1);
    int thrown = hd_keys_ok() && hd_ch(0.5f);
    hd.out_to = hd.mot_to = 0;
    if (thrown)
        hd_throw(hd_rnd(3u) ? hd.low : 12, 0, 60, e, PS_AG_DROP);
    if (hd_ch(chapter ? 0.5f : 0.2f))
        hd_strip(e, 40, PS_AG_DROP, thrown);
}

static void hd_bar(int sec, int at)      /* a bar has begun */
{
    if (!set.auto_perf)
        return;
    if (sec == SEC_MAIN)
        hd_groove(au.bar - au.drop_bar);
    else if (sec == SEC_BREAK)
        hd_break(at);
    else if (ps_ui.sec_bar + 1 >= ps_ui.sec_len)
        hd_drop(au.staged);
}
