/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 hacha */
/* RhinoTiger's engine (psy.h): the kick and the rolling FM bass.
 *
 * The grid: 24 ticks a beat, so that both the sixteenth (6 ticks) and the sixteenth triplet (4) fall on it. The kick
 * is on a beat's first tick; the bass fills the beat's other sixteenths (K-B-B-B) or its other triplet sixteenths
 * (K-b-b-b-b-b). The two never sound at once: the kick is over before the bass's first note, the bass does not start
 * before it. No side-chain, only places. */
#define TPB 24u                        /* ticks a beat */
#define TPBAR (4u * TPB)
#include "psy.h"
#include "fastmath.h"

typedef struct {
    const char *name;
    float bpm;
    int root;                          /* MIDI note of the bass (and the kick's tuning) */
    uint8_t roll;                      /* the bass's step in ticks: 6 sixteenths (K-B-B-B), 4 triplets (K-b-b-b-b-b) */
    float ratio;                       /* the bass's modulator to carrier */
    float index, index_ms;             /* the modulation at the note's start, and how fast it falls (ms to 1/e) */
    float floor;                       /* the modulation it falls to */
    float gate;                        /* the note's length, of its step */
    float drive;
    float kick_len;                    /* the kick's length, of the bass's step */
} world_t;

static const world_t W[PS_NWORLD] = {
    /* cold dark psy: hard, short sixteenths, a dry growl */
    {"COLD SIGNAL", 152.0f, 30, 6, 1.0f, 3.2f, 14.0f, 0.35f, 0.72f, 1.6f, 0.95f},
    /* bright hi-tech: sixteenths at 176, a metallic edge */
    {"GLASS INSECTS", 176.0f, 30, 6, 2.0f, 2.4f, 9.0f, 0.25f, 0.76f, 1.3f, 0.92f},
    /* the same, rolled in triplet sixteenths (a sketch to compare) */
    {"GLASS INSECTS 3", 176.0f, 30, 4, 2.0f, 2.4f, 8.0f, 0.25f, 0.80f, 1.3f, 0.95f},
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
} s;

void ps_init(void)
{
    want_world = world = 0;
    want_play = playing = 1;
    s.k_t = s.b_t = -1.0f;
    s.tick = 0;
    s.next = 0.0f;
}

void ps_world(int w) { want_world = (int)((unsigned)w % PS_NWORLD); }
void ps_play(int on) { want_play = on != 0; }
int ps_playing(void) { return playing; }
int ps_bar(void) { return (int)(s.tick / TPBAR); }

static float note_hz(int note) { return 440.0f * fm_exp2f((float)(note - 69) / 12.0f); }

static void step(const world_t *w)     /* a tick begins: what strikes on it */
{
    uint32_t t = s.tick % TPB, beat = s.tick / TPB % 4u, bar = s.tick / TPBAR;
    int bass_rests = bar % 8u == 7u && beat == 3u;     /* the 8th bar's last beat: the bass stops */
    int kick_rests = bar % 32u == 31u && beat == 3u;   /* the 32nd's: the kick too */
    int bass;
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

static void tempo(const world_t *w)
{
    s.tick_len = PS_SR * 60.0f / w->bpm / (float)TPB;
    s.k_len = w->kick_len * s.tick_len * (float)w->roll / PS_SR;
}

void ps_render(float *out, uint32_t n)
{
    const world_t *w = &W[world];
    float dt = 1.0f / PS_SR;
    uint32_t i;
    if (want_play != playing) {
        playing = want_play;
        s.tick = 0;
        s.next = 0.0f;
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
        out[i] = 0.85f * kick(w, dt) + 0.55f * bass(w, dt);
    }
}
