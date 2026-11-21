/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 hacha */
/* RhinoTiger's events: the one-shot FM sounds that dark psy and hi-tech are made of (zaps, gurgles, dives, a talking
 * growl, chirps, lasers), single or in runs (a staircase of pitches, a roll), and the generator that places them.
 * #included by psy.c (its rnd(), note_hz(), TPB, the world, and the ev_*_now() that the macros lean).
 *
 * A recipe says what an event is (its type, its note, single or a run); a voice plays one note of it. The generator
 * keeps a 4-bar loop of slots on offbeat sixteenths, resolved into recipes as they come round (below, ev_resolve),
 * and Drift redraws a few of them as a phrase (8 bars) begins.
 *
 * The keys: the white keys are sounds, the black keys rhythms. Each white key its own sound (KEY_SOUND, sixteen, low
 * to high), thrown on the next sixteenth; held, again each sixteenth, the sound turning a little as it repeats (a cycle
 * of eight: its depth, its glide, its place left and right). A black key held is a rhythm (KEY_RHYTHM): the white
 * keys held with it throw on its beats instead (alone it is silent); let go, the sixteenths. Pressed harder, a throw
 * bites more. What is thrown is remembered: the generator takes it up later.
 *
 * The keys' loop (REC): two bars of what the keys threw, each where it fell (its tick, on the sixteenths or a rhythm's
 * beats), played again each time round. REC pressed begins a take anew (the old one gone: tapped, nothing in it), the
 * throws while it is held go in, and on the second time round they play under the new ones. Stopped: gone.
 *
 * Their types (ALGORITHM on the EVENTS page), over every recipe, taken as a voice begins:
 *   ALIVE   the recipes as they are
 *   METAL   the modulator off the harmonics, the glides shorter, ringing longer: struck sheet and wire
 *   LIQUID  wobbling slower and deeper, the glides even, longer: things under water
 *   SHARD   short, harsh, the sound held in steps (a lower rate, its aliasing): broken glass, a machine's fault
 *   GHOST   backwards: swelling from nothing, cut at its height, the glide turned round: a sound sucked in
 *   RATCHET chopped on and off 20..30 times a second, longer: a machine's teeth */
/* the types' levels, matched to ALIVE's by the RMS of the events alone (LIQUID's last longer, SHARD's are brighter: a
 * little over and under it). SHARD's steps are its sound: tools/clicks.py counts them. */
#define EV_GAIN_METAL 0.72f
#define EV_GAIN_LIQUID 0.85f
#define EV_GAIN_SHARD 1.0f
#define EV_GAIN_GHOST 1.0f
#define EV_GAIN_RATCHET 1.0f

enum { EV_DIVE, EV_GROWL, EV_ZAP, EV_GURGLE, EV_CHIRP, EV_LASER, EV_NTYPE };
static const char *const EV_NAME[EV_NTYPE] = {"DIVE", "GROWL", "ZAP", "GURGLE", "CHIRP", "LASER"};
enum { RUN_ONE, RUN_STAIRS, RUN_ROLL };

typedef struct {
    uint8_t type, run;
    int8_t note;                       /* MIDI note */
    uint8_t var;                       /* a small, fixed variation (pan, length) */
    uint8_t snd;                       /* a white key's sound (1..16, KEY_SOUND; 0: the type's) */
    uint8_t cyc;                       /* a held key's repeat (1..: its sound turning; 0: none) */
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
    float hx;                          /* SHARD: the sample held, */
    uint8_t hold, hc;                  /* for hold samples (0: none), counted */
    uint8_t rev;                       /* GHOST: the envelope backwards */
    float chop;                        /* RATCHET: on and off so many times a second (0: none) */
    uint8_t cn;                        /* samples to the next control point (0: one now; EV_CTL apart) */
    float cph, mph;
    /* at the control rate (EV_CTL samples), the rest straight between: the pitch (as the carrier's step a sample),
     * the modulation, the gains left and right (the envelope, the level, the pan); now, and at the next point */
    float fi, ix, gl, gr, fi1, ix1, gl1, gr1;
} evoice_t;
#define EV_CTL 16u

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
    /* the keys: pressed, thrown on the next sixteenth (or a rhythm's beat); down, again each */
    uint32_t pending, down;
    int8_t rk;                         /* the black key held whose rhythm leads (-1: none) */
    uint8_t rstart;                    /* its first beat waits for a sixteenth */
    uint8_t rwait;                     /* ticks to its next beat */
    uint32_t ri;                       /* its beats so far */
    uint8_t kcyc[27];                  /* each key's repeats (its sound's cycle) */
    volatile uint8_t press[27];        /* each key's pressure (the UI writes it directly: not through the queue) */
    uint8_t vel[27];
    uint32_t fired;                    /* events begun (the host's checks) */
    int last_type;                     /* the latest thrown (the host's checks) */
} ev;

#define TAKE_TICKS (8u * TPB)          /* the keys' loop: two bars */
#define TAKE_N 96u
static struct {
    struct {
        recipe_t r;
        uint8_t pos;                   /* its tick in the two bars */
        uint8_t bite;                  /* x100 */
    } e[TAKE_N];
    uint8_t n;
    volatile uint8_t rec;              /* REC held: the throws go in */
} take;

static int key_black(int k) { int pc = (k + 5) % 12; return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10; }

/* the white keys' sounds, F3 to G5, each its own: its family (a type: for the palette and the screen), the pitch of
 * the key's note it starts at, how far it glides (octaves) and how (0 even, higher: fast at first), its length and
 * decay, its modulator and modulation (from, to), its wobble (rate, on the pitch, on the modulation), whether it crosses
 * the field, its level (matched by ear-equal RMS) */
typedef struct {
    const char *name;
    uint8_t type;
    float mul, oct, glide_k, dur, tau, ratio, idx0, idx1, lfo_hz, lfo_pitch, lfo_idx;
    uint8_t cross;
    float level;
} key_sound_t;
static const key_sound_t KEY_SOUND[16] = {
    {"DIVE", EV_DIVE, 1.0f, -1.5f, 0.0f, 0.36f, 0.18f, 0.5f, 3.5f, 1.0f, 0.0f, 0.0f, 0.0f, 0, 1.19f},     /* F3 */
    {"GROWL", EV_GROWL, 1.0f, 0.0f, 0.0f, 0.34f, 0.25f, 1.0f, 1.6f, 1.6f, 8.0f, 0.0f, 2.4f, 1, 0.90f},   /* G3 */
    {"CROAK", EV_GROWL, 0.5f, 0.2f, 3.0f, 0.22f, 0.12f, 0.5f, 3.0f, 3.0f, 18.0f, 0.05f, 2.0f, 0, 1.48f}, /* A3 */
    {"SPRING", EV_DIVE, 1.0f, 0.0f, 0.0f, 0.30f, 0.15f, 1.0f, 1.2f, 0.6f, 22.0f, 0.25f, 0.0f, 0, 1.01f}, /* B3 */
    {"SIREN", EV_GURGLE, 1.0f, 0.0f, 0.0f, 0.45f, 0.35f, 2.0f, 1.5f, 1.5f, 3.0f, 0.5f, 0.5f, 1, 0.71f},  /* C4 */
    {"ZAP", EV_ZAP, 8.0f, -4.0f, 6.0f, 0.09f, 0.045f, 1.0f, 4.0f, 0.5f, 0.0f, 0.0f, 0.0f, 1, 1.23f},     /* D4 */
    {"GURGLE", EV_GURGLE, 1.0f, 0.0f, 0.0f, 0.30f, 0.15f, 0.5f, 2.5f, 2.5f, 30.0f, 0.15f, 2.0f, 0, 0.94f},  /* E4 */
    {"SQUELCH", EV_ZAP, 1.0f, 0.3f, 4.0f, 0.12f, 0.07f, 1.0f, 6.0f, 0.3f, 0.0f, 0.0f, 0.0f, 0, 0.92f},  /* F4 */
    {"DROP", EV_CHIRP, 1.0f, 1.2f, 10.0f, 0.09f, 0.05f, 1.0f, 0.4f, 0.4f, 0.0f, 0.0f, 0.0f, 0, 1.12f},  /* G4 */
    {"BUZZ", EV_ZAP, 1.0f, 0.0f, 0.0f, 0.10f, 0.06f, 1.0f, 5.0f, 5.0f, 0.0f, 0.0f, 0.0f, 0, 0.99f},     /* A4 */
    {"RATTLE", EV_GURGLE, 1.0f, 0.0f, 0.0f, 0.25f, 0.14f, 3.3f, 2.5f, 2.5f, 38.0f, 0.0f, 2.5f, 0, 0.71f},  /* B4 */
    {"CHIRP", EV_CHIRP, 2.0f, 1.0f, 8.0f, 0.075f, 0.025f, 3.53f, 2.0f, 0.5f, 0.0f, 0.0f, 0.0f, 0, 1.26f},  /* C5 */
    {"LASER", EV_LASER, 1.0f, 3.0f, 0.0f, 0.15f, 0.08f, 1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1, 1.07f},   /* D5 */
    {"BELL", EV_CHIRP, 2.0f, 0.0f, 0.0f, 0.50f, 0.30f, 2.76f, 1.8f, 0.4f, 0.0f, 0.0f, 0.0f, 0, 0.56f},   /* E5 */
    {"TWEET", EV_LASER, 2.0f, 0.5f, 3.0f, 0.20f, 0.12f, 1.0f, 0.3f, 0.3f, 30.0f, 0.12f, 0.0f, 0, 0.76f},  /* F5 */
    {"SPARK", EV_CHIRP, 2.0f, 0.0f, 0.0f, 0.04f, 0.02f, 7.13f, 8.0f, 2.0f, 0.0f, 0.0f, 0.0f, 0, 1.53f},  /* G5 */
};

/* the black keys' rhythms, F#3 to F#5: the ticks between throws (24 a beat), going round, the first where the tick in
 * the beat modulo grid is phase; a pitch step a throw. Or a pattern of the bar's sixteenths (mask, bit i the i-th):
 * where the bar is, from the next sixteenth (never out of step with the groove, nothing waited for) */
static const struct { uint8_t n; uint8_t iv[13]; uint8_t grid, phase; int8_t step; uint16_t mask; } KEY_RHYTHM[11] = {
    {1, {18}, 6, 0, 0, 0},                                   /* F#3 dotted eighths */
    {1, {12}, 12, 0, 0, 0},                                  /* G#3 eighths (on the eighths) */
    {1, {8}, 8, 0, 0, 0},                                    /* A#3 eighth triplets (on theirs) */
    {0, {0}, 6, 0, 0, 0xDDDDu},                              /* C#4 a gallop, x.xx a beat */
    {0, {0}, 6, 0, 0, 0x4444u},                              /* D#4 the offbeat eighths, ..x. a beat */
    {0, {0}, 6, 0, 0, 0xAAAAu},                              /* F#4 SKIP: the second and fourth sixteenths, .x.x */
    {0, {0}, 6, 0, 0, 0x4949u},                              /* G#4 3-3-2 each half bar, x..x..x. */
    {13, {6, 6, 6, 4, 4, 4, 3, 3, 3, 3, 2, 2, 2}, 6, 0, 0, 0},  /* A#4 faster and faster (two beats) */
    {13, {2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 6, 6, 6}, 6, 0, 0, 0},  /* C#5 slower and slower */
    {1, {6}, 6, 0, 3, 0},                                    /* D#5 sixteenths climbing (a minor third each, eight) */
    {1, {6}, 6, 0, -3, 0},                                   /* F#5 sixteenths falling */
};

static int key_white(int k)            /* the white keys' index (k white) */
{
    int i, n = 0;
    for (i = 0; i < k; i++)
        n += !key_black(i);
    return n;
}
static int key_blacki(int k)           /* the black keys' index (k black) */
{
    int i, n = 0;
    for (i = 0; i < k; i++)
        n += key_black(i);
    return n;
}

static recipe_t key_recipe(int k)      /* key 0 is F3 (MIDI 53): a white key's sound */
{
    recipe_t r;
    int w = key_white(key_black(k) ? k - 1 : k);
    r.note = (int8_t)(53 + k + 12 * key_oct);   /* (OCT- / OCT+) */
    r.type = KEY_SOUND[w].type;
    r.snd = (uint8_t)(w + 1);
    r.run = RUN_ONE;
    r.var = (uint8_t)(k * 37);
    r.cyc = 0;
    return r;
}

static float fvar(uint8_t var, int i) { return (float)((var * (i * 2 + 7) + i * 53) & 63) / 63.0f; }   /* 0..1 */

/* a voice for a recipe's note: i is the note's place in its run, bite 0..1 the key's pressure */
static void ev_voice(const recipe_t *r, int i, float bite, float level)
{
    evoice_t *v = 0;
    int k;
    float f = note_hz(r->note + par[P_EV_PITCH]), a = fvar(r->var, 0), b = fvar(r->var, 1);   /* (PITCH: semitones) */
    int nv = shed ? EV_NVOICE / 2 : EV_NVOICE;   /* (lighter: half of them) */
    for (k = 0; k < nv; k++)
        if (ev.v[k].t < 0.0f) {
            v = &ev.v[k];
            break;
        }
    if (!v) {                          /* all sounding: the oldest gives way */
        v = &ev.v[0];
        for (k = 1; k < nv; k++)
            if (ev.v[k].t > v->t)
                v = &ev.v[k];
    }
    if (r->run == RUN_STAIRS)
        f *= fm_exp2f((float)(i * (r->type <= EV_GROWL ? -3 : 3)) / 12.0f);   /* up a minor third a step (low: down) */
    v->t = 0.0f;
    v->cph = v->mph = 0.0f;
    v->cn = 0;
    v->gl1 = v->gr1 = 0.0f;            /* (its first point: from silence, as its attack began from it) */
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
    if (r->snd) {                      /* a white key's own sound */
        const key_sound_t *ks = &KEY_SOUND[(r->snd - 1u) & 15u];
        v->f0 = f * ks->mul; v->oct = ks->oct; v->glide_k = ks->glide_k;
        v->dur = ks->dur * (0.9f + 0.2f * b); v->tau = ks->tau;
        v->ratio = ks->ratio; v->idx0 = ks->idx0; v->idx1 = ks->idx1;
        v->lfo_hz = ks->lfo_hz * (0.9f + 0.2f * a); v->lfo_pitch = ks->lfo_pitch; v->lfo_idx = ks->lfo_idx;
        v->pan0 *= ks->cross ? 1.0f : 0.5f;
        v->pan1 = ks->cross ? -v->pan0 : v->pan0;
        v->level *= ks->level;
    }
    v->hold = 0;
    v->rev = 0;
    v->chop = 0.0f;
    switch (s.type[PART_EVENTS]) {
    case 1:                            /* METAL */
        v->ratio += 1.41f;
        v->oct *= 0.5f;
        v->lfo_pitch *= 0.3f;
        v->dur *= 1.3f;
        v->tau *= 1.8f;
        v->idx0 *= 0.8f;
        v->idx1 = v->idx0 * 0.6f;
        v->level *= EV_GAIN_METAL;
        break;
    case 2:                            /* LIQUID */
        if (v->lfo_hz > 0.0f) {
            v->lfo_hz *= 0.5f;
            v->lfo_idx *= 1.5f;
        } else {
            v->lfo_hz = 5.0f + 4.0f * a;
            v->lfo_pitch = 0.04f;
            v->lfo_idx = 1.2f;
        }
        v->glide_k = 0.0f;
        if (v->ratio > 2.0f)
            v->ratio = 1.0f;
        v->dur *= 1.4f;
        v->tau *= 1.5f;
        v->idx0 *= 0.8f;
        v->idx1 *= 0.8f;
        v->level *= EV_GAIN_LIQUID;
        break;
    case 3:                            /* SHARD */
        v->ratio = v->ratio * 2.0f + 0.76f;
        v->dur *= 0.55f;
        v->tau *= 0.5f;
        v->idx0 *= 1.6f;
        v->idx1 *= 1.6f;
        v->hold = (uint8_t)(5u + (uint32_t)(4.0f * b));   /* about 9..5 kHz */
        v->hc = 0;
        v->level *= EV_GAIN_SHARD;
        break;
    case 4:                            /* GHOST */
        v->rev = 1;
        v->f0 *= fm_exp2f(v->oct);     /* (the glide from where it would end, back to where it would start) */
        v->oct = -v->oct;
        v->dur *= 1.3f;
        v->lfo_pitch *= 0.5f;
        v->level *= EV_GAIN_GHOST;
        break;
    case 5:                            /* RATCHET */
        v->chop = 20.0f + 10.0f * b;
        v->dur *= 1.4f;
        v->tau *= 2.0f;
        v->level *= EV_GAIN_RATCHET;
        break;
    }
    if (r->cyc) {                      /* a held key's repeat: the sound turning through a cycle of eight */
        float p = (float)((r->cyc - 1u) % 8u) / 8.0f, sn = fm_sin_turns(p), cs = fm_sin_turns(p + 0.25f);
        v->idx0 *= 1.0f + 0.5f * sn;
        v->idx1 *= 1.0f + 0.5f * sn;
        v->oct *= 1.0f + 0.35f * cs;
        v->ratio *= 1.0f + 0.06f * sn;
        v->pan0 = 0.6f * sn;
        v->pan1 = 0.6f * cs;
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
    r.snd = r.cyc = 0;
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

#define KEY_BLACKS 0x252A52Au             /* the black keys' bits: F#3 G#3 A#3 C#4 D#4 F#4 G#4 A#4 C#5 D#5 F#5 */

static void key_throw(int k, int stepped)   /* key k's sound now (its cycle on; stepped: semitones, a climbing rhythm) */
{
    recipe_t r = key_recipe(k);
    float bite = 0.5f * (float)ev.vel[k] / 127.0f + (float)ev.press[k] / 127.0f;
    r.note = (int8_t)clampi(r.note + stepped, 24, 108);
    r.cyc = (uint8_t)(1u + ev.kcyc[k]++ % 8u);
    {   /* the keys' loop: the same sound on the same tick already in it (a key held round again): it plays, once */
        uint32_t pos = s.tick % TAKE_TICKS, i;
        for (i = 0; i < take.n; i++)
            if (take.e[i].pos == pos && take.e[i].r.snd == r.snd && take.e[i].r.note == r.note)
                break;
        if (i == take.n) {
            ev_start(&r, bite, 1.0f);
            if (take.rec && take.n < TAKE_N) {   /* into the keys' loop, where it fell */
                take.e[take.n].r = r;
                take.e[take.n].pos = (uint8_t)pos;
                take.e[take.n].bite = (uint8_t)(bite * 100.0f);
                take.n++;
            }
        }
    }
    ev.last_type = EV_NTYPE + r.snd - 1;   /* (the screen names it: the key's sound) */
    ps_ui.thrown_type = (int8_t)ev.last_type;
    ps_ui.hits[HIT_THROW]++;
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
    if (take.n) {                      /* the keys' loop (before the keys: a throw taken now plays next time round) */
        uint32_t pos = s.tick % TAKE_TICKS, i;
        for (i = 0; i < take.n; i++)
            if (take.e[i].pos == pos) {
                ev_start(&take.e[i].r, (float)take.e[i].bite / 100.0f, 1.0f);
                ps_ui.hits[HIT_EVENT]++;
            }
    }
    if (ev.rk >= 0 && KEY_RHYTHM[key_blacki(ev.rk)].mask) {   /* a pattern's: on its sixteenths of the bar */
        ev.rstart = 0;
        if (t % 6u == 0u && (KEY_RHYTHM[key_blacki(ev.rk)].mask >> (pos16 % 16u) & 1u)) {
            uint32_t whites = (ev.down | ev.pending) & ~KEY_BLACKS;
            ev.pending = 0;
            for (k = 0; k < 27; k++)
                if (whites >> k & 1u)
                    key_throw(k, 0);
            ev.ri++;
        }
    } else if (ev.rk >= 0 && !(ev.rstart && t % KEY_RHYTHM[key_blacki(ev.rk)].grid != KEY_RHYTHM[key_blacki(ev.rk)].phase) &&
        (ev.rstart || --ev.rwait == 0u)) {   /* a rhythm's beat (its first on its grid) */
        int b = key_blacki(ev.rk), stepped = (int)(ev.ri % 8u) * (int)KEY_RHYTHM[b].step;
        uint32_t whites = (ev.down | ev.pending) & ~KEY_BLACKS;   /* (held, or tapped since its last beat) */
        ev.rstart = 0;
        ev.pending = 0;                /* (the white keys wait for its beats) */
        for (k = 0; k < 27; k++)       /* (none held: its beats go by unheard) */
            if (whites >> k & 1u)
                key_throw(k, stepped);
        ev.rwait = KEY_RHYTHM[b].iv[ev.ri % KEY_RHYTHM[b].n];
        ev.ri++;
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
    if (ev.rk < 0) {                   /* no rhythm: the white keys on the sixteenths */
        uint32_t keys = (ev.pending | ev.down) & ~KEY_BLACKS;   /* the newly pressed (if let go since), and the held
                                                                * (again each sixteenth; a rhythm let go: on) */
        ev.pending = 0;
        for (k = 0; keys && k < 27; k++)
            if (keys >> k & 1u)
                key_throw(k, 0);
    }
}

static void ev_key(int k, int down, int press)   /* (render: from the command queue): down 1 (press: velocity), 0 up */
{
    if (k < 0 || k >= 27)
        return;
    if (down) {
        recipe_t r = key_recipe(k);
        if (key_black(k)) {            /* a rhythm while held (its beats from the next sixteenth; the latest leads) */
            ev.down |= 1u << k;
            ev.rk = (int8_t)k;
            ev.ri = 0;
            ev.rstart = 1;
            return;
        }
        ev.kcyc[k] = 0;                /* its sound's cycle from the start */
        ev.pending |= 1u << k;
        ev.down |= 1u << k;
        ev.vel[k] = (uint8_t)press;    /* (on a press, the value is the velocity) */
        ev.mem[ev.memw] = r;           /* remembered: the generator takes it up later */
        ev.memw = (uint8_t)((ev.memw + 1u) % EV_MEM);
        if (ev.nmem < EV_MEM)
            ev.nmem++;
    } else {
        ev.down &= ~(1u << k);
        if (ev.rk == k) {              /* its rhythm let go: another black key still held, or the sixteenths */
            int b;
            ev.rk = -1;
            for (b = 26; b >= 0; b--)
                if (key_black(b) && ev.down >> b & 1u) {
                    ev.rk = (int8_t)b;
                    ev.ri = 0;
                    ev.rstart = 1;
                    break;
                }
        }
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

/* a voice's control point at t (seconds into it): the carrier's step a sample, the modulation, the gains */
static void ev_ctl(evoice_t *v, float t, float dt, float wide, float law, float cross)
{
    float u = t < v->dur ? t / v->dur : 1.0f, g, f, lfo = 0.0f, amp, pan, p1;
    g = v->glide_k > 0.0f ? (1.0f - fm_expf(-v->glide_k * u)) / (1.0f - fm_expf(-v->glide_k)) : u;
    f = v->f0 * fm_exp2f(v->oct * g);
    if (v->lfo_hz > 0.0f) {            /* (its phase from the start: the rate is fixed) */
        float ph = v->lfo_hz * t;
        lfo = fm_sin_turns(ph - fm_floorf(ph));
        f *= 1.0f + v->lfo_pitch * lfo;
    }
    if (f > 16000.0f)
        f = 16000.0f;
    v->fi1 = f * dt;
    v->ix1 = (v->idx0 + (v->idx1 - v->idx0) * u + v->lfo_idx * lfo) * 0.159155f;
    if (v->rev)                        /* GHOST: swelling, cut at its height (~6% of it) */
        amp = u * u * (u < 0.94f ? 1.0f : (1.0f - u) / 0.06f) * v->level * 0.28f;
    else
        amp = (t < 0.001f ? t / 0.001f : 1.0f) * fm_expf(-t / v->tau) * (1.0f - u * u) * v->level * 0.28f;
    if (v->chop > 0.0f) {              /* RATCHET: on the first 55% of each turn, its edges ~1.8 ms slopes */
        float c = v->chop * t, e = 0.0018f * v->chop;
        c -= fm_floorf(c);
        amp *= fm_clampf(fm_minf(c, 0.55f - c) / e, 0.0f, 1.0f);
    }
    p1 = v->pan1 + (-v->pan0 - v->pan1) * cross;   /* (SPREAD past 50: crossing) */
    pan = fm_clampf((v->pan0 + (p1 - v->pan0) * u) * wide, -1.0f, 1.0f);
    v->gl1 = amp * (1.0f - law * pan);
    v->gr1 = amp * (1.0f + law * pan);
}

static float ev_render(float dt, float *l, float *r)
{
    /* SPREAD: 0 all in the middle, 50 as each event has it, 100 three times as far (to the sides), the pan's law
     * from half to whole (hard left, hard right), and each crossing to the other side as it sounds */
    float send = 0.0f, el = 0.0f, er = 0.0f, sp = (float)par[P_EV_SPREAD] / 50.0f,
          wide = sp <= 1.0f ? sp : 1.0f + 2.0f * (sp - 1.0f), law = 0.5f + 0.25f * (wide - 1.0f > 0.0f ? wide - 1.0f : 0.0f),
          cross = sp > 1.0f ? sp - 1.0f : 0.0f;
    int k;
    for (k = 0; k < EV_NVOICE; k++) {
        evoice_t *v = &ev.v[k];
        float x;
        if (v->t < 0.0f)
            continue;
        if (v->t >= v->dur) {
            v->t = -1.0f;
            continue;
        }
        if (!v->cn) {                  /* a control point: on from where the last one aimed, to the next */
            if (v->t == 0.0f)
                ev_ctl(v, 0.0f, dt, wide, law, cross);
            v->fi = v->fi1;
            v->ix = v->ix1;
            v->gl = v->gl1;
            v->gr = v->gr1;
            ev_ctl(v, v->t + (float)EV_CTL * dt, dt, wide, law, cross);
            v->cn = EV_CTL;
        }
        {
            const float w = 1.0f / (float)EV_CTL, a = (float)(EV_CTL - v->cn) * w;   /* (its share of the way) */
            float fi = v->fi + (v->fi1 - v->fi) * a, ix = v->ix + (v->ix1 - v->ix) * a;
            v->mph += fi * v->ratio;
            v->mph -= fm_floorf(v->mph);
            v->cph += fi;
            v->cph -= fm_floorf(v->cph);
            x = fm_sin_turns(v->cph + ix * fm_sin_turns(v->mph));
            if (v->hold) {             /* (SHARD) held in steps */
                if (v->hc == 0u)
                    v->hx = x;
                if (++v->hc >= v->hold)
                    v->hc = 0;
                x = v->hx;
            }
            {                          /* tanh(1.4 x) for x in -1..1: an odd polynomial (within 2e-5), no exp, no divide */
                float z = x * x;
                x *= 1.39991761f + z * (-0.912032723f + z * (0.692545849f + z * (-0.466315745f + z * (0.222434431f +
                     z * -0.0512179515f))));
            }
            el += x * (v->gl + (v->gl1 - v->gl) * a);
            er += x * (v->gr + (v->gr1 - v->gr) * a);
        }
        v->cn--;
        v->t += dt;
    }
    el = hp2(&ev_hp2[0], ev_highpass(0, el), 0.9747f);   /* (twice: 24 dB an octave, a dive's depths out) */
    er = hp2(&ev_hp2[1], ev_highpass(1, er), 0.9747f);
    *l += el;
    *r += er;
    send = 0.5f * (el + er);           /* the echo hears them high-passed too */
    return send;
}
