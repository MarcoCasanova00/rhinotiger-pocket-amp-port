/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 hacha */
/* RhinoTiger's engine (psy.h): the kick, the rolling FM bass and the percussion.
 *
 * The grid: 24 ticks a beat, so that both the sixteenth (6 ticks) and the sixteenth triplet (4) fall on it. The kick
 * is on a beat's first tick; the bass fills the beat's other sixteenths (K-B-B-B) or its other triplet sixteenths
 * (K-b-b-b-b-b). The two never sound at once: the kick is over before the bass's first note, the bass does not start
 * before it. No side-chain, only places.
 *
 * The percussion: closed hats on the sixteenths (the offbeat eighth left to the open hat), a clap on 2 and 4, a ride
 * on the eighths in some worlds. They come in over the first bars (hats, then the open hat, the clap, the ride) and
 * now and then one sits out a phrase (8 bars). A fill's beat (the bass resting) is a clap roll. */
#define TPB 24u                        /* ticks a beat */
#define TPBAR (4u * TPB)
#include "psy.h"
#include "fastmath.h"

typedef struct {
    const char *name;
    float bpm;
    int root;                          /* MIDI note of the bass (and the kick's tuning) */
    uint8_t roll;                      /* the bass's step in ticks: 6 sixteenths (K-B-B-B), the rule; 4 triplets
                                        * (K-b-b-b-b-b), a rare colour (the user: "there may be some, but rare") */
    float ratio;                       /* the bass's modulator to carrier */
    float index, index_ms;             /* the modulation at the note's start, and how fast it falls (ms to 1/e) */
    float floor;                       /* the modulation it falls to */
    float gate;                        /* the note's length, of its step */
    float drive;
    float kick_len;                    /* the kick's length, of the bass's step */
    /* the percussion */
    float hat_ms, open_ms;             /* the closed and the open hat's decay (ms to 1/e) */
    float hat_hp;                      /* the hats' high-pass, 0..1 (higher: thinner) */
    float clap_hz;                     /* the clap's band */
    uint8_t ride;                      /* a ride on the eighths */
    /* the events (events.c) */
    uint8_t ev_density;                /* recipes in the 4-bar loop */
    uint8_t ev_low, ev_mid;            /* % of them low (dive, growl) and middle (zap, gurgle); the rest high */
    uint8_t ev_runs;                   /* % of them runs */
} world_t;

static const world_t W[PS_NWORLD] = {
    /* cold dark psy: hard, short sixteenths, a dry growl */
    {"COLD SIGNAL", 152.0f, 30, 6, 1.0f, 3.2f, 14.0f, 0.35f, 0.72f, 1.6f, 0.95f, 14.0f, 55.0f, 0.80f, 1100.0f, 0, 5, 30, 55, 15},
    /* bright hi-tech: sixteenths at 176, a metallic edge */
    {"GLASS INSECTS", 176.0f, 30, 6, 2.0f, 2.4f, 9.0f, 0.25f, 0.76f, 1.3f, 0.92f, 10.0f, 40.0f, 0.88f, 1500.0f, 1, 9, 10, 40, 35},
};

const char *ps_world_name(int w) { return W[(unsigned)w % PS_NWORLD].name; }

static volatile int want_world, want_play;
static int world, playing;

static struct {
    float tick_len, next;              /* samples a tick, and samples to the next */
    uint32_t tick;                     /* ticks since PLAY */
    /* the kick */
    float k_t, k_ph;                   /* seconds since it struck (< 0: silent), its phase (turns) */
    float k_len;
    /* the bass */
    float b_t, b_gate;                 /* seconds since the note, how long it is held */
    float b_cph, b_mph, b_inc;         /* carrier and modulator phase (turns), the carrier's step */
    float b_amp;                       /* the release's level */
    float lp;                          /* the bass's one-pole low-pass */
    float drift;                       /* the slow movement: 0..1 over 16 bars */
    /* the percussion: each a hit's age (s, < 0 silent) and level */
    float h_t, h_v, o_t, o_v, c_t, c_v, r_t, r_v;
    float h_hp1, h_hp2, h_x1, h_x2;    /* the hats' two one-pole high-passes (shared by closed and open) */
    float c_low, c_band;               /* the clap's band-pass (state variable) */
    float r_ph[3];                     /* the ride's partials */
    uint32_t rng;
    uint8_t out;                       /* the part sitting out this phrase: 0 none, 1 hats, 2 open, 3 clap, 4 ride */
    /* the echo: a dotted eighth, ping-pong */
    float e_lp[2];
} s;

#define ECHO_N 16384u
static int16_t echo_buf[2][ECHO_N];
static uint32_t echo_w;

/* commands from the UI: keys (the render drains them as a block begins) */
#define CMD_N 64u
static volatile uint32_t cmd_q[CMD_N], cmd_w, cmd_r;
static void cmd_post(uint32_t c)
{
    uint32_t w = cmd_w;
    if (w - cmd_r >= CMD_N)
        return;                        /* full: dropped */
    cmd_q[w % CMD_N] = c;
    cmd_w = w + 1u;
}
void ps_key(int k, int down, int value) { cmd_post((uint32_t)(k & 31) | (uint32_t)(down & 3) << 5 | (uint32_t)(value & 127) << 8); }

static uint32_t rnd(void);
static float note_hz(int note);
#include "events.c"

void ps_init(void)
{
    want_world = world = 0;
    want_play = playing = 1;
    s.k_t = s.b_t = -1.0f;
    s.h_t = s.o_t = s.c_t = s.r_t = -1.0f;
    s.rng = 0x9E3779B9u;
    for (int k = 0; k < EV_NVOICE; k++)
        ev.v[k].t = -1.0f;
    s.tick = 0;
    s.next = 0.0f;
}

void ps_world(int w) { want_world = (int)((unsigned)w % PS_NWORLD); }
void ps_play(int on) { want_play = on != 0; }
int ps_playing(void) { return playing; }
int ps_bar(void) { return (int)(s.tick / TPBAR); }

static float note_hz(int note) { return 440.0f * fm_exp2f((float)(note - 69) / 12.0f); }
int ps_stat(int which) { return which == 0 ? (int)ev.fired : which == 1 ? ev.nmem : ev.last_type; }
const char *ps_event_name(int type) { return EV_NAME[(unsigned)type % EV_NTYPE]; }

static uint32_t rnd(void)
{
    s.rng ^= s.rng << 13;
    s.rng ^= s.rng >> 17;
    s.rng ^= s.rng << 5;
    return s.rng;
}

static void perc_step(const world_t *w, uint32_t t, uint32_t beat, uint32_t bar, int fill)
{
    uint32_t six = t / 6u;             /* the sixteenth in the beat */
    static const float HAT_V[4] = {0.55f, 0.80f, 0.0f, 1.0f};
    if (t % 6u)
        return;
    if (t == 0u && beat == 0u && bar % 8u == 0u) {   /* a phrase begins: who sits it out */
        s.out = 0;
        if (bar >= 32u && rnd() % 4u == 0u)
            s.out = (uint8_t)(1u + rnd() % (w->ride ? 4u : 3u));
    }
    if (fill) {                        /* the fill's beat: a clap roll, rising */
        s.c_t = 0.0f;
        s.c_v = 0.45f + 0.18f * (float)six;
        return;
    }
    if (six != 2u && s.out != 1u) {
        s.h_t = 0.0f;
        s.h_v = HAT_V[six] * (0.85f + 0.15f * (float)(rnd() % 100u) / 100.0f);
    }
    if (six == 2u && bar >= 8u && s.out != 2u) {
        s.o_t = 0.0f;
        s.o_v = 1.0f;
    }
    if (six == 0u && (beat == 1u || beat == 3u) && bar >= 16u && s.out != 3u) {
        s.c_t = 0.0f;
        s.c_v = 1.0f;
    }
    if (w->ride && six % 2u == 0u && bar >= 24u && s.out != 4u) {
        s.r_t = 0.0f;
        s.r_v = six == 2u ? 1.0f : 0.6f;
    }
}

static void step(const world_t *w)     /* a tick begins: what strikes on it */
{
    uint32_t t = s.tick % TPB, beat = s.tick / TPB % 4u, bar = s.tick / TPBAR;
    int bass_rests = bar % 8u == 7u && beat == 3u;     /* the 8th bar's last beat: the bass stops */
    int kick_rests = bar % 32u == 31u && beat == 3u;   /* the 32nd's: the kick too */
    int bass;
    perc_step(w, t, beat, bar, bass_rests);
    ev_step(w, t, bar, (uint32_t)(s.tick / 6u), 0);
    if (t == 0u) {
        if (!kick_rests) {
            s.k_t = 0.0f;
            s.k_ph = 0.0f;
        }
        return;
    }
    bass = t % w->roll == 0u;
    if (!bass || bass_rests)
        return;
    s.b_t = 0.0f;
    s.b_cph = s.b_mph = 0.0f;          /* every note from the same place: the roll is tight */
    s.b_gate = w->gate * s.tick_len * (float)w->roll / PS_SR;
    s.b_inc = note_hz(w->root) / PS_SR;
}

static float kick(const world_t *w, float dt)
{
    float f0, f, env, x, t = s.k_t;
    if (t < 0.0f)
        return 0.0f;
    if (t >= s.k_len) {
        s.k_t = -1.0f;
        return 0.0f;
    }
    f0 = note_hz(w->root);                          /* tuned to the key */
    f = f0 + f0 * 9.0f * fm_expf(-t / 0.011f);      /* the pitch falls from about 9 octaves' worth of sweep */
    s.k_ph += f * dt;
    if (s.k_ph >= 1.0f)
        s.k_ph -= 1.0f;
    env = fm_expf(-t / (s.k_len * 0.45f));
    env *= 1.0f - t / s.k_len;                      /* to nothing by its end: the bass's place stays clear */
    x = fm_sin_turns(s.k_ph) * env;
    x += (t < 0.002f ? 0.35f * (1.0f - t / 0.002f) : 0.0f) * fm_sin_turns(s.k_ph * 7.0f);   /* the click */
    s.k_t = t + dt;
    return fm_tanhf(x * 1.8f);
}

static float bass(const world_t *w, float dt)
{
    float t = s.b_t, idx, env, x, m;
    if (t < 0.0f)
        return s.lp *= 0.99f;
    if (t < s.b_gate)
        env = t < 0.001f ? t / 0.001f : 1.0f;
    else {
        env = 1.0f - (t - s.b_gate) / 0.003f;      /* a 3 ms release */
        if (env <= 0.0f) {
            s.b_t = -1.0f;
            env = 0.0f;
        }
    }
    s.b_amp = env;
    idx = w->index * (0.75f + 0.5f * s.drift);     /* the slow movement opens and closes it */
    idx = idx * fm_expf(-t * 1000.0f / w->index_ms) + w->floor;
    m = fm_sin_turns(s.b_mph);
    x = fm_sin_turns(s.b_cph + idx * m * 0.159155f) * env;
    s.b_cph += s.b_inc;
    if (s.b_cph >= 1.0f)
        s.b_cph -= 1.0f;
    s.b_mph += s.b_inc * w->ratio;
    if (s.b_mph >= 1.0f)
        s.b_mph -= 1.0f;
    if (s.b_t >= 0.0f)
        s.b_t = t + dt;
    x = fm_tanhf(x * w->drive);
    s.lp += (x - s.lp) * 0.45f;                    /* takes the edge off the drive (about 4 kHz) */
    return s.lp;
}

static float noise(void) { return (float)(int32_t)rnd() * (1.0f / 2147483648.0f); }

/* the percussion, a sample: l and r added to */
static void perc(const world_t *w, float dt, float *l, float *r)
{
    float n = noise(), hp, x;
    /* the hats: noise through two high-passes */
    s.h_hp1 = w->hat_hp * (s.h_hp1 + n - s.h_x1);
    s.h_x1 = n;
    s.h_hp2 = w->hat_hp * (s.h_hp2 + s.h_hp1 - s.h_x2);
    s.h_x2 = s.h_hp1;
    hp = s.h_hp2;
    if (s.h_t >= 0.0f) {
        x = hp * s.h_v * fm_expf(-s.h_t * 1000.0f / w->hat_ms) * 0.20f;
        *l += 0.8f * x;
        *r += 1.1f * x;
        if ((s.h_t += dt) > w->hat_ms * 0.008f)
            s.h_t = -1.0f;
    }
    if (s.o_t >= 0.0f) {
        x = hp * s.o_v * fm_expf(-s.o_t * 1000.0f / w->open_ms) * 0.22f;
        *l += 1.05f * x;
        *r += 0.85f * x;
        if ((s.o_t += dt) > w->open_ms * 0.008f)
            s.o_t = -1.0f;
    }
    /* the clap: a band of noise, three quick bursts, then its tail */
    if (s.c_t >= 0.0f) {
        float t = s.c_t, f = 2.0f * fm_sinf(3.14159265f * w->clap_hz / PS_SR), hi, env;
        hi = n - s.c_low - 0.9f * s.c_band;
        s.c_band += f * hi;
        s.c_low += f * s.c_band;
        if (t < 0.024f) {
            float u = t - 0.008f * fm_floorf(t / 0.008f);
            env = fm_expf(-u / 0.0025f);
        } else {
            env = 0.8f * fm_expf(-(t - 0.024f) / 0.045f);
        }
        x = s.c_band * env * s.c_v * 1.4f;
        *l += x;
        *r += x;
        if ((s.c_t += dt) > 0.3f)
            s.c_t = -1.0f;
    }
    /* the ride: three inharmonic partials and a little of the hats' noise */
    if (s.r_t >= 0.0f) {
        static const float RH[3] = {3120.0f, 4617.0f, 7394.0f};
        float m = 0.0f, env = fm_expf(-s.r_t / 0.16f);
        int i;
        for (i = 0; i < 3; i++) {
            s.r_ph[i] += RH[i] * dt;
            if (s.r_ph[i] >= 1.0f)
                s.r_ph[i] -= 1.0f;
            m += fm_sin_turns(s.r_ph[i]);
        }
        x = (0.35f * m + 0.6f * hp) * env * s.r_v * 0.05f;
        *l += 1.15f * x;
        *r += 0.75f * x;
        if ((s.r_t += dt) > 0.9f)
            s.r_t = -1.0f;
    }
}

static void tempo(const world_t *w)
{
    s.tick_len = PS_SR * 60.0f / w->bpm / (float)TPB;
    s.k_len = w->kick_len * s.tick_len * (float)w->roll / PS_SR;
}

void ps_render(float *outl, float *outr, uint32_t n)
{
    const world_t *w = &W[world];
    float dt = 1.0f / PS_SR;
    uint32_t i;
    if (want_play != playing) {
        playing = want_play;
        s.tick = 0;
        s.next = 0.0f;
    }
    while (cmd_r != cmd_w) {           /* the keys */
        uint32_t c = cmd_q[cmd_r % CMD_N];
        cmd_r++;
        ev_key((int)(c & 31u), (int)(c >> 5 & 3u), (int)(c >> 8 & 127u));
    }
    tempo(w);
    for (i = 0; i < n; i++) {
        if (playing) {
            if (s.next <= 0.0f) {                  /* a tick begins on this sample */
                if (s.tick % TPBAR == 0u && want_world != world) {   /* a world changes as a bar begins */
                    world = want_world;
                    w = &W[world];
                    tempo(w);
                }
                s.drift = 0.5f - 0.5f * fm_cosf((float)(s.tick % (16u * TPBAR)) / (float)(16u * TPBAR) * 6.2831853f);
                step(w);
                s.tick++;
                s.next += s.tick_len;
            }
            s.next -= 1.0f;
        }
        {
            float c = 0.85f * kick(w, dt) + 0.55f * bass(w, dt), l = c, r = c, send, el, er;
            uint32_t d = (uint32_t)(s.tick_len * 18.0f);   /* a dotted eighth: 18 ticks */
            perc(w, dt, &l, &r);
            send = ev_render(dt, &l, &r);
            if (d >= ECHO_N)
                d = ECHO_N - 1u;
            el = (float)echo_buf[0][(echo_w - d) % ECHO_N] * (1.0f / 32768.0f);
            er = (float)echo_buf[1][(echo_w - d) % ECHO_N] * (1.0f / 32768.0f);
            s.e_lp[0] += (er * 0.42f + send * 0.5f - s.e_lp[0]) * 0.35f;   /* into the left: the right's, and the send */
            s.e_lp[1] += (el * 0.42f - s.e_lp[1]) * 0.35f;                  /* into the right: the left's */
            echo_buf[0][echo_w % ECHO_N] = (int16_t)(fm_clampf(s.e_lp[0], -0.99f, 0.99f) * 32767.0f);
            echo_buf[1][echo_w % ECHO_N] = (int16_t)(fm_clampf(s.e_lp[1], -0.99f, 0.99f) * 32767.0f);
            echo_w++;
            outl[i] = l + 0.7f * el;
            outr[i] = r + 0.7f * er;
        }
    }
}
