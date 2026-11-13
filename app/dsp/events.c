/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 hacha */
/* RhinoTiger's events: the one-shot FM sounds that dark psy and hi-tech are made of (zaps, gurgles, dives, a talking
 * growl, chirps, lasers), single or in runs (a staircase of pitches, a roll), and the generator that places them.
 * #included by psy.c (its rnd(), note_hz(), TPB, the world).
 *
 * A recipe says what an event is (its type, its note, single or a run); a voice plays one note of it. The generator
 * keeps a 4-bar loop of recipes on sixteenths and changes one or two of them as a phrase (8 bars) begins. The keys
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
    recipe_t loop[EV_LOOP];
    uint8_t on[EV_LOOP];               /* a recipe there */
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
    int last_type;                     /* the latest thrown (the screen) */
} ev;

static int key_black(int k) { int pc = (k + 5) % 12; return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; }

static recipe_t key_recipe(int k)      /* key 0 is F3 (MIDI 53) */
{
    recipe_t r;
    int fam = k < 9 ? 0 : k < 18 ? 1 : 2;
    r.note = (int8_t)(53 + k);
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
            ev.run[k].left = 3;
            ev.run[k].step = 6;        /* sixteenths */
            ev.run[k].wait = 6;
            ev.run[k].i = 1;
            ev.run[k].bite = bite;
            return;
        }
}

/* the generator: a recipe of the world's */
static recipe_t ev_invent(const world_t *w)
{
    static const int8_t SCALE[8] = {0, 1, 3, 6, 7, 10, 12, 13};   /* dark: seconds and tritones about the root */
    recipe_t r;
    uint32_t x = rnd();
    if (ev.nmem && x % 100u < 10u + 5u * ev.nmem)   /* something a key threw: more often the more were thrown (to 50%) */
        return ev.mem[(x >> 8) % ev.nmem];
    x = rnd() % 100u;
    r.type = (uint8_t)(x < w->ev_low ? EV_DIVE + rnd() % 2u
                       : x < w->ev_low + w->ev_mid ? EV_ZAP + rnd() % 2u : EV_CHIRP + rnd() % 2u);
    r.note = (int8_t)(w->root + 36 + SCALE[rnd() % 8u] + (r.type >= EV_CHIRP ? 12 : r.type <= EV_GROWL ? -12 : 0));
    r.run = rnd() % 100u < w->ev_runs ? (r.type <= EV_GROWL ? RUN_ROLL : RUN_STAIRS) : RUN_ONE;
    r.var = (uint8_t)rnd();
    return r;
}

static void ev_place(const world_t *w)   /* one recipe into the loop, on an offbeat sixteenth where it is free */
{
    uint32_t p, tries;
    for (tries = 0; tries < 16u; tries++) {
        p = rnd() % EV_LOOP;
        if (p % 4u && !ev.on[p]) {
            ev.loop[p] = ev_invent(w);
            ev.on[p] = 1;
            return;
        }
    }
}

static void ev_phrase(const world_t *w, uint32_t bar)   /* a phrase begins: the loop changes a little */
{
    uint32_t n = 0, p, k;
    for (p = 0; p < EV_LOOP; p++)
        n += ev.on[p];
    if (bar == 0u) {
        for (p = 0; p < EV_LOOP; p++)
            ev.on[p] = 0;
        n = 0;
    }
    for (k = 0; n && k < 1u + rnd() % 2u; k++) {   /* one or two out */
        p = rnd() % EV_LOOP;
        while (!ev.on[p])
            p = (p + 1u) % EV_LOOP;
        ev.on[p] = 0;
        n--;
    }
    while (n < w->ev_density) {
        ev_place(w);
        n++;
    }
}

/* a tick: the loop's events, the runs, the keys' throws */
static void ev_step(const world_t *w, uint32_t t, uint32_t bar, uint32_t pos16, int quiet)
{
    int k;
    if (t == 0u && pos16 % 16u == 0u && bar % 8u == 0u)   /* a phrase's first tick */
        ev_phrase(w, bar);
    for (k = 0; k < 4; k++)
        if (ev.run[k].left && !--ev.run[k].wait) {
            ev_voice(&ev.run[k].r, ev.run[k].i++, ev.run[k].bite, 0.8f);
            ev.run[k].wait = ev.run[k].step;
            ev.run[k].left--;
        }
    if (t % 6u)
        return;
    if (bar >= 4u && !quiet && ev.on[pos16 % EV_LOOP])
        ev_start(&ev.loop[pos16 % EV_LOOP], 0.0f, 0.7f);
    if (ev.pending | ev.held) {        /* the keys: the newly pressed, and the held again */
        uint32_t keys = ev.pending | ev.held;
        ev.pending = 0;
        for (k = 0; k < 27; k++)
            if (keys >> k & 1u) {
                recipe_t r = key_recipe(k);
                float bite = 0.5f * (float)ev.vel[k] / 127.0f + (float)ev.press[k] / 127.0f;
                ev_start(&r, bite, 1.0f);
                ev.last_type = r.type;
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
    el = ev_highpass(0, el);
    er = ev_highpass(1, er);
    *l += el;
    *r += er;
    send = 0.5f * (el + er);           /* the echo hears them high-passed too */
    return send;
}
