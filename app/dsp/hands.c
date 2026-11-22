/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 hacha */
/* AUTO PLAY's hands (psy.c includes this; psy.h ps_auto_do): its effects, its parts out, its throws, beside the
 * player's and never in them (fx.held, fx.out, fx.muted, the keys and their loop stay the player's). Each is a thing
 * from a tick to a tick (since PLAY), so each is on the grid to the sample and none outlives its end: at each tick what
 * is on is worked out anew. A section's change ends them all, but those meant to go on (PS_AG_BREAK, into a break;
 * PS_AG_DROP, from a drop's first tick on). Resting and PLAY's beginning: none.
 *
 * A buffer's effect gives way to a key's and to a build's own. A throw's pitch is the sound's key's, moved to the
 * root, and so many semitones; one throw at a time (mono, among themselves). */
#define AG_N 32u
static ps_ag_t ag[AG_N];               /* (t1 0: none) */
#define AQ_N 32u
static ps_ag_t aq[AQ_N];               /* from the UI */
static volatile uint32_t aq_w, aq_r;
static ps_motif_t ag_mot[2];           /* the UI fills the one not handed over last */
static volatile uint8_t ag_mot_w;
static uint32_t ag_thrown;             /* (the host's checks) */
static uint32_t ag_lit[16];            /* each sound's latest throw (its tick + 1; 0 none): the keys' lights */
static uint8_t ag_do;                  /* for the screen: 1 half time, 2 a fill, 4 a gap, now */

static void aq_post(const ps_ag_t *g)
{
    uint32_t w = aq_w;
    if (w - aq_r >= AQ_N)
        return;
    aq[w % AQ_N] = *g;
    aq_w = w + 1u;
}
void ps_auto_do(const ps_ag_t *g) { aq_post(g); }
void ps_auto_clear(uint32_t kinds)
{
    ps_ag_t g = {.kind = 255, .id = (uint8_t)kinds};
    aq_post(&g);
}
void ps_auto_back(int part)
{
    ps_ag_t g = {.kind = 254, .id = (uint8_t)(1u << part)};
    aq_post(&g);
}
ps_motif_t *ps_auto_motif(int *id)
{
    ag_mot_w ^= 1u;
    *id = ag_mot_w;
    return &ag_mot[ag_mot_w];
}

static void ag_none(void)
{
    uint32_t k;
    for (k = 0; k < AG_N; k++)
        ag[k].t1 = 0;
    for (k = 0; k < 16u; k++)
        ag_lit[k] = 0;
    ag_do = 0;
    fx.auto_held = 0;
    fx.auto_key = -1;
    fx.auto_out = fx.auto_half = 0;
}

static void ag_reset(void)             /* (render: PLAY begins or rests) */
{
    aq_r = aq_w;
    ag_none();
}

static void ag_drain(void)             /* (render, as a block begins) */
{
    while (aq_r != aq_w) {
        ps_ag_t g = aq[aq_r % AQ_N];
        uint32_t k;
        aq_r++;
        if (g.kind == 255) {
            for (k = 0; k < AG_N; k++)
                if (ag[k].t1 && (g.id >> ag[k].kind & 1u))
                    ag[k].t1 = 0;
            continue;
        }
        if (g.kind == 254) {
            for (k = 0; k < AG_N; k++)
                if (ag[k].t1 && ag[k].kind == PS_AG_OUT && !(ag[k].id &= (uint8_t)~g.id))
                    ag[k].t1 = 0;
            continue;
        }
        if (!playing || g.t1 <= s.tick)   /* (too late) */
            continue;
        for (k = 0; k < AG_N; k++)
            if (!ag[k].t1) {
                ag[k] = g;
                break;
            }
    }
}

static void ag_section(int sec)        /* a section begins (enter) */
{
    uint32_t k;
    for (k = 0; k < AG_N; k++)
        if (ag[k].t1 && !(sec == SEC_BREAK && (ag[k].flag & PS_AG_BREAK)) &&
            !(sec == SEC_MAIN && (ag[k].flag & PS_AG_DROP) && ag[k].t0 >= s.tick))
            ag[k].t1 = 0;
}

static void ag_throw(uint32_t snd, int semi, float bite, uint32_t i)   /* a white key's sound, in the root's key */
{
    static const uint8_t KEY[16] = {53, 55, 57, 59, 60, 62, 64, 65, 67, 69, 71, 72, 74, 76, 77, 79};
    recipe_t r;
    int n = KEY[snd & 15u], d = ((m.root + s.lift - n) % 12 + 18) % 12 - 6;   /* (the root's nearest) */
    uint32_t k;
    r.type = KEY_SOUND[snd & 15u].type;
    r.snd = (uint8_t)(1u + (snd & 15u));
    r.note = (int8_t)clampi(n + d + semi, 36, 104);
    r.run = RUN_ONE;
    r.var = (uint8_t)(snd * 37u + i * 11u);
    r.cyc = (uint8_t)(1u + i % 8u);
    for (k = 0; k < EV_NVOICE; k++)
        if (ev.v[k].t >= 0.0f && ev.v[k].live == 2u && ev.v[k].ck == 0.0f)
            ev.v[k].ck = ev.v[k].t + 0.015f;
    ev.live_next = 2;
    ev_start(&r, bite, 0.85f);
    ag_thrown++;
    ag_lit[snd & 15u] = s.tick + 1u;
    ps_ui.hits[HIT_EVENT]++;
}

static void ag_step(void)              /* a tick: what is on */
{
    uint32_t t = s.tick, k, i, at = 0;
    uint16_t held = 0;
    uint8_t out = 0, now = 0;
    int8_t key = -1;
    for (k = 0; k < AG_N; k++) {
        const ps_ag_t *g = &ag[k];
        if (!g->t1)
            continue;
        if (t >= g->t1) {
            ag[k].t1 = 0;
            continue;
        }
        if (g->kind == PS_AG_OUT && t + 1u >= g->t0 && t + 1u < g->t1)   /* (a tick early: gone, and back, before the
                                                                          * beat's stroke) */
            out |= g->id;
        if (t < g->t0)
            continue;
        switch (g->kind) {
        case PS_AG_FX:
            if (g->id > FX_R8T && g->id != FX_REVERSE && g->id != FX_TAPE && g->id != FX_STAIRS)
                held |= (uint16_t)(1u << g->id);
            else if (key < 0 || g->t0 >= at) {   /* (the buffer's: the latest) */
                key = (int8_t)g->id;
                at = g->t0;
            }
            break;
        case PS_AG_HALF:
            now |= 1u;
            break;
        case PS_AG_FILL:
            now |= 2u;
            fx.fill_req |= t == g->t0;
            break;
        case PS_AG_GAP:
            now |= 4u;
            fx.gap_req |= t == g->t0;
            break;
        case PS_AG_THROW:
            if (t == g->t0)
                ag_throw(g->id, g->semi, (float)g->bite / 100.0f, t / 6u);
            break;
        case PS_AG_MOTIF: {
            const ps_motif_t *mo = &ag_mot[g->id & 1u];
            uint32_t pos = mo->len ? (t - g->t0) % mo->len : 0u;
            for (i = 0; i < mo->n && i < PS_MOTIF_N; i++)
                if (mo->e[i].pos == pos)
                    ag_throw(mo->e[i].snd, mo->e[i].semi, (float)mo->e[i].bite / 100.0f, i);
            break;
        }
        }
    }
    fx.auto_held = held;
    fx.auto_key = key;
    fx.auto_out = out;
    fx.auto_half = now & 1u;
    ag_do = now;
}

static uint32_t ag_lights(void)        /* the sounds thrown just now, 1 << the white key's */
{
    uint32_t k, m = 0, t = s.tick;
    for (k = 0; k < 16u; k++)
        if (ag_lit[k] && t - ag_lit[k] < 5u)
            m |= 1u << k;
    return m;
}
