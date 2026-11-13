/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 hacha */
/* RhinoTiger's events: the one-shot FM sounds that dark psy and hi-tech are made of (zaps, gurgles, dives, a talking
 * growl, chirps, lasers), single or in runs (a staircase of pitches, a roll), and the generator that places them.
 * #included by psy.c (its rnd(), note_hz(), TPB, the world, and the ev_*_now() that the macros lean).
 *
 * A recipe says what an event is (its type, its note, single or a run); a voice plays one note of it. The generator
 * keeps a 4-bar loop of slots on offbeat sixteenths, resolved into recipes as they come round (below, ev_resolve),
 * and Drift redraws a few of them as a phrase (8 bars) begins. The keys
 * throw recipes in, on the next sixteenth: the register says the family (low: dive, growl; middle: zap, gurgle;
 * high: chirp, laser), the key the note, a white key one event, a black key a run. A key held goes on throwing, a
 * sixteenth apart; pressed harder, it bites more. What is thrown is remembered: the generator takes it up later. */

enum { EV_DIVE, EV_GROWL, EV_ZAP, EV_GURGLE, EV_CHIRP, EV_LASER, EV_NTYPE };
static const char *const EV_NAME[EV_NTYPE] = {"DIVE", "GROWL", "ZAP", "GURGLE", "CHIRP", "LASER"};
enum { RUN_ONE, RUN_STAIRS, RUN_ROLL };

typedef struct {
    uint8_t type, run;
    int8_t note;                       /* MIDI note */
    uint8_t var;                       /* a small, fixed variation (pan, length) */
} recipe_t;

#define EV_NVOICE 8
typedef struct {
    float t, dur;                      /* seconds since it began (< 0: free), its length */
    float f0, oct;                     /* the starting pitch (Hz) and how far it glides (octaves) */
    float glide_k;                     /* the glide's shape: 0 even, higher: fast at first */
    float ratio, idx0, idx1;           /* the modulator's ratio, the modulation from idx0 to idx1 */
    float lfo_hz, lfo_pitch, lfo_idx;  /* a wobble: its rate, on the pitch (fraction), on the modulation */
    float tau;                         /* the decay (s to 1/e) */
    float pan0, pan1;                  /* -1 left .. 1 right, from and to */
    float level;
    float cph, mph, lph;
} evoice_t;

#define EV_LOOP 64u                    /* the generator's loop: 4 bars of sixteenths */
#define EV_MEM 8
static struct {
    evoice_t v[EV_NVOICE];
    struct ev_slot {
        uint8_t pos;                   /* its sixteenth in the loop (0xFF: none) */
        uint8_t fam, sub, deg, lean, run, var;   /* its dice (see ev_resolve) */
        uint8_t fixed;                 /* r: something a key threw, played as it was */
        recipe_t r;
    } slot[24];
    uint8_t at[EV_LOOP];               /* the slot on each sixteenth (0xFF: none) */
    uint8_t density;                   /* the slots heard (ranks below it) */
    recipe_t mem[EV_MEM];              /* what the keys threw, the latest last */
    uint8_t nmem, memw;
    /* runs under way: the generator's and the keys' */
    struct {
        recipe_t r;
        uint8_t left, step;            /* notes still to come, ticks apart */
        uint8_t wait;                  /* ticks to the next */
        uint8_t i;                     /* the notes so far */
        float bite;
    } run[4];
    /* the keys: thrown on the next sixteenth; held, again each sixteenth */
    uint32_t held, pending, down;
    volatile uint8_t press[27];        /* each key's pressure (the UI writes it directly: not through the queue) */
    uint8_t vel[27];
    uint32_t fired;                    /* events begun (the host's checks) */
    int last_type;                     /* the latest thrown (the host's checks) */
} ev;

static int key_black(int k) { int pc = (k + 5) % 12; return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; }

static recipe_t key_recipe(int k)      /* key 0 is F3 (MIDI 53) */
{
    recipe_t r;
    int fam = k < 9 ? 0 : k < 18 ? 1 : 2;
    r.note = (int8_t)(53 + k + 12 * key_oct);   /* (OCT- / OCT+) */
    r.type = (uint8_t)(fam * 2 + ((k / 2) & 1));   /* neighbouring keys alternate the family's two */
    r.run = key_black(k) ? (fam == 0 ? RUN_ROLL : RUN_STAIRS) : RUN_ONE;
    r.var = (uint8_t)(k * 37);
    return r;
}

static float fvar(uint8_t var, int i) { return (float)((var * (i * 2 + 7) + i * 53) & 63) / 63.0f; }   /* 0..1 */

/* a voice for a recipe's note: i is the note's place in its run, bite 0..1 the key's pressure */
static void ev_voice(const recipe_t *r, int i, float bite, float level)
{
    evoice_t *v = 0;
    int k;
    float f = note_hz(r->note), a = fvar(r->var, 0), b = fvar(r->var, 1);
    for (k = 0; k < EV_NVOICE; k++)
        if (ev.v[k].t < 0.0f) {
            v = &ev.v[k];
            break;
        }
    if (!v) {                          /* all sounding: the oldest gives way */
        v = &ev.v[0];
        for (k = 1; k < EV_NVOICE; k++)
            if (ev.v[k].t > v->t)
                v = &ev.v[k];
    }
    if (r->run == RUN_STAIRS)
        f *= fm_exp2f((float)(i * (r->type <= EV_GROWL ? -3 : 3)) / 12.0f);   /* up a minor third a step (low: down) */
    v->t = 0.0f;
    v->cph = v->mph = v->lph = 0.0f;
    v->glide_k = 0.0f;
    v->lfo_hz = v->lfo_pitch = v->lfo_idx = 0.0f;
    v->pan0 = 0.8f * (2.0f * a - 1.0f);
    v->pan1 = v->pan0;
    v->level = level;
    switch (r->type) {
    case EV_DIVE:                      /* falls an octave and a half, growling (above the bass: no lower than it) */
        v->f0 = f; v->oct = -1.5f; v->dur = 0.32f + 0.1f * b; v->tau = 0.18f;
        v->ratio = 0.5f; v->idx0 = 3.5f; v->idx1 = 1.0f;
        v->pan0 *= 0.3f; v->pan1 = v->pan0;
        break;
    case EV_GROWL:                     /* a low voice that seems to talk: its modulation wobbles */
        v->f0 = f; v->oct = 0.0f; v->dur = 0.30f + 0.12f * b; v->tau = 0.25f;
        v->ratio = 1.0f; v->idx0 = v->idx1 = 1.6f;
        v->lfo_hz = 6.0f + 4.0f * a; v->lfo_idx = 2.4f;
        v->pan0 *= 0.4f; v->pan1 = -v->pan0;
        break;
    case EV_ZAP:                       /* drops from high, fast */
        v->f0 = f * 8.0f; v->oct = -4.0f; v->glide_k = 6.0f; v->dur = 0.07f + 0.05f * b; v->tau = 0.045f;
        v->ratio = 1.0f; v->idx0 = 4.0f; v->idx1 = 0.5f;
        v->pan1 = -v->pan0;
        break;
    case EV_GURGLE:                    /* bubbling: the pitch and the modulation shake fast */
        v->f0 = f; v->oct = 0.0f; v->dur = 0.24f + 0.12f * b; v->tau = 0.15f;
        v->ratio = 0.5f; v->idx0 = v->idx1 = 2.5f;
        v->lfo_hz = 24.0f + 16.0f * a; v->lfo_pitch = 0.15f; v->lfo_idx = 2.0f;
        break;
    case EV_CHIRP:                     /* a short metallic blip, up */
        v->f0 = f * 2.0f; v->oct = 1.0f; v->glide_k = 8.0f; v->dur = 0.06f + 0.03f * b; v->tau = 0.025f;
        v->ratio = 3.53f; v->idx0 = 2.0f; v->idx1 = 0.5f;
        break;
    default:                           /* EV_LASER: a sweep up three octaves, crossing the field */
        v->f0 = f; v->oct = 3.0f; v->dur = 0.12f + 0.06f * b; v->tau = 0.08f;
        v->ratio = 1.0f; v->idx0 = v->idx1 = 1.0f;
        v->pan1 = -v->pan0;
        break;
    }
    if (r->run == RUN_ROLL)
        v->idx0 += 0.5f * (float)i;    /* a roll opens as it goes */
    v->idx0 *= 1.0f + 1.2f * bite;
    v->idx1 *= 1.0f + 1.2f * bite;
    ev.fired++;
}

static void ev_start(const recipe_t *r, float bite, float level)   /* a recipe begins: its first note, its run */
{
    int k;
    ev_voice(r, 0, bite, level);
    if (r->run == RUN_ONE)
        return;
    for (k = 0; k < 4; k++)
        if (!ev.run[k].left) {
            ev.run[k].r = *r;
            ev.run[k].left = (uint8_t)ev_run_len();
            ev.run[k].step = (uint8_t)ev_run_step();   /* sixteenths, or 32nds (Detail) */
            ev.run[k].wait = ev.run[k].step;
            ev.run[k].i = 1;
            ev.run[k].bite = bite;
            return;
        }
}

/* the generator: a recipe of the world's */
/* The generator's loop. Each slot keeps fixed draws (where it is, its rank, and dice for its family, note, lean and
 * run); what it plays is resolved from them against the macros and the EVENTS page as it comes round, so a knob is
 * heard within a sixteenth and can be turned back: Density plays the slots ranked below it, Dark tilts the family
 * and the note, Runs and Detail decide single or run. A slot may instead hold something a key threw, as it was. */
#define EV_SLOTS 24u                   /* the most a loop can hold (Density at the top) */
static void ev_draw(uint32_t i)        /* slot i (its rank): new dice, a new free offbeat place */
{
    uint32_t p, tries;
    ev.slot[i].pos = 0xFF;
    for (tries = 0; tries < 32u; tries++) {
        p = rnd() % EV_LOOP;
        if (p % 4u && ev.at[p] == 0xFF) {
            ev.slot[i].pos = (uint8_t)p;
            ev.at[p] = (uint8_t)i;
            break;
        }
    }
    ev.slot[i].fam = (uint8_t)(rnd() % 100u);
    ev.slot[i].sub = (uint8_t)(rnd() % 2u);
    ev.slot[i].deg = (uint8_t)(rnd() % 8u);
    ev.slot[i].lean = (uint8_t)(rnd() % 100u);
    ev.slot[i].run = (uint8_t)(rnd() % 100u);
    ev.slot[i].var = (uint8_t)rnd();
    ev.slot[i].fixed = 0;
    if (ev.nmem && rnd() % 100u < 10u + 5u * ev.nmem) {   /* something a key threw: the more thrown, the more often */
        ev.slot[i].fixed = 1;
        ev.slot[i].r = ev.mem[rnd() % ev.nmem];
    }
}

static void ev_undraw(uint32_t i)
{
    if (ev.slot[i].pos != 0xFF)
        ev.at[ev.slot[i].pos] = 0xFF;
    ev.slot[i].pos = 0xFF;
}

static recipe_t ev_resolve(const world_t *w, uint32_t i)   /* what slot i plays now */
{
    static const int8_t SCALE[8] = {0, 1, 3, 6, 7, 10, 12, 13};   /* dark: seconds and tritones about the root */
    static const int8_t OPEN[4] = {0, 7, 12, 19};                 /* (Dark turned down) fifths and octaves */
    static const int8_t SHADE[4] = {1, 6, 13, 18};                /* (Dark turned up) seconds and tritones */
    const struct ev_slot *sl = &ev.slot[i];
    recipe_t r;
    uint32_t low = (uint32_t)ev_low_now(w), mid = (uint32_t)ev_mid_now(w);
    int8_t deg = SCALE[sl->deg];
    float dk = ev_dark();
    if (sl->fixed)
        return sl->r;
    r.type = (uint8_t)((sl->fam < low ? EV_DIVE : sl->fam < low + mid ? EV_ZAP : EV_CHIRP) + sl->sub);
    if ((float)sl->lean < 60.0f * (dk < 0.0f ? -dk : dk))         /* Dark leans the note one way or the other */
        deg = dk < 0.0f ? OPEN[sl->deg % 4u] : SHADE[sl->deg % 4u];
    r.note = (int8_t)(m.root + 36 + deg + (r.type >= EV_CHIRP ? 12 : r.type <= EV_GROWL ? -12 : 0));
    r.run = sl->run < (uint32_t)ev_runs_now(w) ? (r.type <= EV_GROWL ? RUN_ROLL : RUN_STAIRS) : RUN_ONE;
    r.var = sl->var;
    return r;
}

static void ev_phrase(uint32_t bar)    /* a phrase begins: Drift redraws a few of the slots that play */
{
    uint32_t i, k, changes = (uint32_t)ev_changes(), live = 0;
    if (bar == 0u) {
        for (i = 0; i < EV_LOOP; i++)
            ev.at[i] = 0xFF;
        for (i = 0; i < EV_SLOTS; i++) {
            ev.slot[i].pos = 0xFF;
            ev_draw(i);
        }
        return;
    }
    for (i = 0; i < EV_SLOTS; i++)
        live += i < ev.density;
    for (k = 0; live && k < changes; k++) {
        i = rnd() % live;              /* one of those heard now */
        ev_undraw(i);
        ev_draw(i);
    }
}

/* a tick: the loop's events, the runs, the keys' throws */
static void ev_step(const world_t *w, uint32_t t, uint32_t bar, uint32_t pos16, int quiet)
{
    int k;
    if (t == 0u && pos16 % 16u == 0u && bar % 8u == 0u)   /* a phrase's first tick */
        ev_phrase(bar);
    ev.density = (uint8_t)ev_density_now(w);
    for (k = 0; k < 4; k++)
        if (ev.run[k].left && !--ev.run[k].wait) {
            ev_voice(&ev.run[k].r, ev.run[k].i++, ev.run[k].bite, 0.8f);
            ev.run[k].wait = ev.run[k].step;
            ev.run[k].left--;
        }
    if (t % 6u)
        return;
    {
        uint32_t i = ev.at[pos16 % EV_LOOP];
        if (!quiet && i < ev.density) {   /* a slot here, ranked within the density (quiet: the first bars, a build) */
            recipe_t r = ev_resolve(w, i);
            ev_start(&r, 0.0f, 0.7f);
            ps_ui.hits[HIT_EVENT]++;
        }
    }
    if (ev.pending | ev.held) {        /* the keys: the newly pressed, and the held again */
        uint32_t keys = ev.pending | ev.held;
        ev.pending = 0;
        for (k = 0; k < 27; k++)
            if (keys >> k & 1u) {
                recipe_t r = key_recipe(k);
                float bite = 0.5f * (float)ev.vel[k] / 127.0f + (float)ev.press[k] / 127.0f;
                ev_start(&r, bite, 1.0f);
                ev.last_type = r.type;
                ps_ui.thrown_type = (int8_t)r.type;
                ps_ui.hits[HIT_THROW]++;
            }
        ev.held = keys & ev.down;      /* still down: again on the next sixteenth */
    }
}

static void ev_key(int k, int down, int press)   /* (render: from the command queue): down 1 (press: velocity), 0 up */
{
    if (k < 0 || k >= 27)
        return;
    if (down) {
        recipe_t r = key_recipe(k);
        ev.pending |= 1u << k;
        ev.down |= 1u << k;
        ev.vel[k] = (uint8_t)press;    /* (on a press, the value is the velocity) */
        ev.mem[ev.memw] = r;           /* remembered: the generator takes it up later */
        ev.memw = (uint8_t)((ev.memw + 1u) % EV_MEM);
        if (ev.nmem < EV_MEM)
            ev.nmem++;
    } else {
        ev.down &= ~(1u << k);
        ev.held &= ~(1u << k);
    }
}

/* a sample of the events: l and r added to, and the echo's send */
static float ev_hp[2][2], ev_hx[2][2];
static hp2_t ev_hp2[2];
static float ev_highpass(int ch, float x)   /* ~180 Hz, 12 dB an octave: the events keep out of the bass's lane */
{
    const float a = 0.9747f;           /* exp(-2 pi 180 / 44100) */
    float y1 = a * (ev_hp[ch][0] + x - ev_hx[ch][0]), y2;
    ev_hx[ch][0] = x;
    ev_hp[ch][0] = y1;
    y2 = a * (ev_hp[ch][1] + y1 - ev_hx[ch][1]);
    ev_hx[ch][1] = y1;
    ev_hp[ch][1] = y2;
    return y2;
}

static float ev_render(float dt, float *l, float *r)
{
    float send = 0.0f, el = 0.0f, er = 0.0f;
    int k;
    for (k = 0; k < EV_NVOICE; k++) {
        evoice_t *v = &ev.v[k];
        float u, g, f, idx, x, amp, pan, lfo;
        if (v->t < 0.0f)
            continue;
        if (v->t >= v->dur) {
            v->t = -1.0f;
            continue;
        }
        u = v->t / v->dur;
        g = v->glide_k > 0.0f ? (1.0f - fm_expf(-v->glide_k * u)) / (1.0f - fm_expf(-v->glide_k)) : u;
        f = v->f0 * fm_exp2f(v->oct * g);
        lfo = 0.0f;
        if (v->lfo_hz > 0.0f) {
            v->lph += v->lfo_hz * dt;
            if (v->lph >= 1.0f)
                v->lph -= 1.0f;
            lfo = fm_sin_turns(v->lph);
            f *= 1.0f + v->lfo_pitch * lfo;
        }
        if (f > 16000.0f)
            f = 16000.0f;
        idx = v->idx0 + (v->idx1 - v->idx0) * u + v->lfo_idx * lfo;
        v->mph += f * v->ratio * dt;
        v->mph -= fm_floorf(v->mph);
        v->cph += f * dt;
        v->cph -= fm_floorf(v->cph);
        x = fm_sin_turns(v->cph + idx * fm_sin_turns(v->mph) * 0.159155f);
        amp = (v->t < 0.001f ? v->t / 0.001f : 1.0f) * fm_expf(-v->t / v->tau) * (1.0f - u * u);
        x = fm_tanhf(1.4f * x) * amp * v->level * 0.28f;
        pan = v->pan0 + (v->pan1 - v->pan0) * u;
        el += x * (1.0f - 0.5f * pan);
        er += x * (1.0f + 0.5f * pan);
        v->t += dt;
    }
    el = hp2(&ev_hp2[0], ev_highpass(0, el), 0.9747f);   /* (twice: 24 dB an octave, a dive's depths out) */
    er = hp2(&ev_hp2[1], ev_highpass(1, er), 0.9747f);
    *l += el;
    *r += er;
    send = 0.5f * (el + er);           /* the echo hears them high-passed too */
    return send;
}
