/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 hacha */
/* RhinoTiger's engine (psy.h): the kick, the rolling FM bass, the percussion, the events (events.c) and an echo.
 *
 * The grid: 24 ticks a beat, so that both the sixteenth (6 ticks) and the sixteenth triplet (4) fall on it. The kick
 * is on a beat's first tick; the bass fills the beat's other sixteenths (K-B-B-B) or its other triplet sixteenths
 * (K-b-b-b-b-b). The two never sound at once: the kick is over before the bass's first note, the bass does not start
 * before it. No side-chain, only places.
 *
 * The percussion: closed hats on the sixteenths (the offbeat eighth left to the open hat), a clap on 2 and 4, a ride
 * on the eighths in some worlds. They come in over the first bars (hats, then the open hat, the clap, the ride) and
 * now and then one sits out a phrase (8 bars). A fill's beat (the bass resting) is a clap roll.
 *
 * The macros (MAIN) lean several things at once, each from its middle (50: the world as it is):
 *   Energy   the events' density, the hats' weak sixteenths, how often a part sits out
 *   Detail   the events' runs (how often, how long, sixteenths or 32nds), the hats' 32nd flams
 *   Dark     the events' families (low and middle against high) and notes (seconds and tritones against fifths
 *            and octaves), the bass's brightness
 *   Drift    how much of the events' loop changes as a phrase begins, how far the bass slowly opens and closes, how
 *            often a break comes on its own
 *
 * The sections (psy.h): the groove; a break, the kick, the bass and the percussion out, the atmos up and the events
 * fewer; its build, four bars of a clap roll quickening (quarters, eighths, sixteenths, and the last beat silent: a
 * breath) under a noise rising; the drop, on a bar's first beat: a crash, the phrases counted from it, the events'
 * loop drawn anew, and the bass a shade other (another of four colours: its ratio and its bite). A cue (SAVE) acts as
 * the next bar begins; a break on its own comes as a 32-bar block begins, 64 bars after the last drop at least, and
 * lasts as the world has it (then builds and drops on its own); a cued one waits for the next cue.
 *
 * The atmos: a held chord of three notes over the root (fifths and the octave; at high Dark a second and a tritone),
 * each a little apart left and right and lightly frequency-modulated, and a band of noise wandering (Air). Quiet under
 * the groove as the world has it, up in a break, back down after the drop.
 *
 * Memory (the device's pool, 336 KB): the template's 68 KB, the echo's 64 KB, the effects' buffer 128 KB (fx.c): about
 * 76 KB left. What changes slowly is worked out as a block begins (coefs()), not every sample. */
#define TPB PS_TPB                     /* ticks a beat */
#define TPBAR (4u * TPB)
#include "psy.h"
#include "fastmath.h"
#include "plat.h"                     /* PLAT_POOL: the big buffers in the pool on the device */

typedef struct {
    const char *name;
    float bpm;
    int root;                          /* MIDI note of the bass (and the kick's tuning) */
    /* the bass: K-B-B-B, and now and then a phrase of another shape (the user: triplets "there may be some, but
     * rare"; a gallop, K-.-B-B, may sit better) */
    uint8_t trip, gallop;              /* % of phrases in sixteenth triplets (K-b-b-b-b-b), in a gallop */
    uint8_t skip, jump;                /* % of notes resting (a stutter), jumping (an octave or a fifth up) */
    float ratio;                       /* the bass's modulator to carrier */
    float index, index_ms;             /* the modulation at the note's start, and how fast it falls (ms to 1/e) */
    float floor;                       /* the modulation it falls to */
    float gate;                        /* the note's length, of its step */
    float drive;
    float kick_len;                    /* the kick's length, of the bass's step */
    float kick_bright;                 /* its punch and click, of Punch's */
    /* the percussion */
    float hat_ms, open_ms;             /* the closed and the open hat's decay (ms to 1/e) */
    float hat_hp;                      /* the hats' high-pass, 0..1 (higher: thinner) */
    float clap_hz;                     /* the clap's band */
    uint8_t ride;                      /* a ride on the eighths */
    /* the events (events.c) */
    uint8_t ev_density;                /* recipes in the 4-bar loop */
    uint8_t ev_low, ev_mid;            /* % of them low (dive, growl) and middle (zap, gurgle); the rest high */
    uint8_t ev_runs;                   /* % of them runs */
    /* the atmos and the sections */
    float atmos;                       /* the atmos under the groove, of its level in a break */
    uint8_t brk;                       /* a break's bars when it comes on its own */
    /* the line (line.c) */
    float line;                        /* the share of phrases it plays (0: none) */
    float line_dens;                   /* its steps on, of the most */
    int8_t line_oct;                   /* its register over the root, semitones */
    uint8_t line_range;                /* how far its notes go up, semitones */
    uint8_t line_chrom, line_slide;    /* % of steps creeping a semitone, sliding to the next */
    float line_ratio, line_index, line_ms;   /* its FM: ratio, the modulation and its fall (ms to 1/e) */
    float line_gate;                   /* its notes' length, of their step */
    float line_wah;                    /* the modulation swinging with the note, 0..1 */
} world_t;

/* the eight worlds, slow and wet to fast and fine (notes/psytrance.md 6) */
static const world_t W[PS_NWORLD] = {
    /* low and muffled, a soft stuttering bass, wet clicks, a thick murmur */
    {.name = "SWAMP RADIO", .bpm = 145, .root = 29, .gallop = 15, .skip = 9,
     .ratio = 1.0f, .index = 1.8f, .index_ms = 22, .floor = 0.2f, .gate = 0.82f, .drive = 1.1f, .kick_len = 1.0f, .kick_bright = 0.5f,
     .hat_ms = 18, .open_ms = 70, .hat_hp = 0.70f, .clap_hz = 900,
     .ev_density = 6, .ev_low = 35, .ev_mid = 45, .ev_runs = 10, .atmos = 0.8f, .brk = 16,
     .line = 0.1f, .line_dens = 0.25f, .line_oct = 24, .line_range = 7, .line_chrom = 30, .line_slide = 30,
     .line_ratio = 1.0f, .line_index = 1.5f, .line_ms = 60, .line_gate = 0.6f, .line_wah = 0.3f},
    /* deep and long, a thick low bass, a growl now and then, sinking sounds, a dark swell; long breaks */
    {.name = "UNDER THE ROOTS", .bpm = 148, .root = 28, .gallop = 10,
     .ratio = 0.5f, .index = 2.6f, .index_ms = 18, .floor = 0.3f, .gate = 0.8f, .drive = 1.5f, .kick_len = 1.0f, .kick_bright = 0.7f,
     .hat_ms = 14, .open_ms = 60, .hat_hp = 0.78f, .clap_hz = 1000,
     .ev_density = 4, .ev_low = 55, .ev_mid = 30, .ev_runs = 10, .atmos = 0.55f, .brk = 24,
     .line = 0.35f, .line_dens = 0.3f, .line_oct = 12, .line_range = 5, .line_chrom = 50, .line_slide = 50,
     .line_ratio = 1.5f, .line_index = 3.0f, .line_ms = 120, .line_gate = 0.9f, .line_wah = 0.2f},
    /* cold dark psy: hard, short sixteenths, a short phrase creeping in semitones, lasers, a thin band of noise */
    {.name = "COLD SIGNAL", .bpm = 152, .root = 30, .gallop = 10,
     .ratio = 1.0f, .index = 3.2f, .index_ms = 14, .floor = 0.35f, .gate = 0.72f, .drive = 1.6f, .kick_len = 0.95f, .kick_bright = 1.0f,
     .hat_ms = 14, .open_ms = 55, .hat_hp = 0.80f, .clap_hz = 1100,
     .ev_density = 5, .ev_low = 30, .ev_mid = 55, .ev_runs = 15, .atmos = 0.2f, .brk = 16,
     .line = 0.6f, .line_dens = 0.35f, .line_oct = 24, .line_range = 4, .line_chrom = 70, .line_slide = 10,
     .line_ratio = 3.0f, .line_index = 2.5f, .line_ms = 25, .line_gate = 0.45f},
    /* bouncing: the bass jumps now and then, a talking FM line, gurgles; sudden turns */
    {.name = "RUBBER CIRCUS", .bpm = 156, .root = 31, .trip = 5, .gallop = 20, .jump = 12,
     .ratio = 1.0f, .index = 3.0f, .index_ms = 16, .floor = 0.3f, .gate = 0.6f, .drive = 1.4f, .kick_len = 0.9f, .kick_bright = 1.2f,
     .hat_ms = 12, .open_ms = 50, .hat_hp = 0.82f, .clap_hz = 1300,
     .ev_density = 7, .ev_low = 20, .ev_mid = 60, .ev_runs = 20, .atmos = 0.1f, .brk = 8,
     .line = 0.7f, .line_dens = 0.45f, .line_oct = 24, .line_range = 9, .line_chrom = 20, .line_slide = 35,
     .line_ratio = 1.0f, .line_index = 4.0f, .line_ms = 40, .line_gate = 0.6f, .line_wah = 0.8f},
    /* heavy, a distorted bass, a long dragging phrase, groans; big breaks */
    {.name = "BLACK UNDERTOW", .bpm = 160, .root = 28, .gallop = 10,
     .ratio = 1.0f, .index = 3.6f, .index_ms = 20, .floor = 0.45f, .gate = 0.75f, .drive = 2.2f, .kick_len = 1.0f, .kick_bright = 1.1f,
     .hat_ms = 14, .open_ms = 55, .hat_hp = 0.78f, .clap_hz = 1000,
     .ev_density = 9, .ev_low = 50, .ev_mid = 35, .ev_runs = 15, .atmos = 0.25f, .brk = 16,
     .line = 0.6f, .line_dens = 0.25f, .line_oct = 12, .line_range = 6, .line_chrom = 50, .line_slide = 60,
     .line_ratio = 2.0f, .line_index = 3.5f, .line_ms = 200, .line_gate = 1.0f, .line_wah = 0.1f},
    /* tight, a short metallic repeat, the staircases begin; now and then a phrase of triplets */
    {.name = "COPPER NERVE", .bpm = 168, .root = 29, .trip = 15, .gallop = 10,
     .ratio = 2.0f, .index = 2.8f, .index_ms = 11, .floor = 0.3f, .gate = 0.72f, .drive = 1.5f, .kick_len = 0.93f, .kick_bright = 1.1f,
     .hat_ms = 11, .open_ms = 45, .hat_hp = 0.85f, .clap_hz = 1300, .ride = 1,
     .ev_density = 8, .ev_low = 20, .ev_mid = 45, .ev_runs = 30, .atmos = 0.1f, .brk = 8,
     .line = 0.7f, .line_dens = 0.4f, .line_oct = 36, .line_range = 5, .line_chrom = 30, .line_slide = 5,
     .line_ratio = 3.5f, .line_index = 2.5f, .line_ms = 18, .line_gate = 0.5f},
    /* bright hi-tech: clean sixteenths, a bright metallic lead, staircases flying left and right; short breaks */
    {.name = "GLASS INSECTS", .bpm = 176, .root = 30, .gallop = 5,
     .ratio = 2.0f, .index = 2.4f, .index_ms = 9, .floor = 0.25f, .gate = 0.76f, .drive = 1.3f, .kick_len = 0.92f, .kick_bright = 1.2f,
     .hat_ms = 10, .open_ms = 40, .hat_hp = 0.88f, .clap_hz = 1500, .ride = 1,
     .ev_density = 9, .ev_low = 10, .ev_mid = 40, .ev_runs = 35, .atmos = 0.05f, .brk = 8,
     .line = 0.6f, .line_dens = 0.4f, .line_oct = 36, .line_range = 12, .line_slide = 15,
     .line_ratio = 2.0f, .line_index = 2.0f, .line_ms = 30, .line_gate = 0.6f},
    /* fast and fine: the detail in the line's fragments and the events, the densest; changing all the time */
    {.name = "CHROME SWARM", .bpm = 186, .root = 31,
     .ratio = 2.0f, .index = 2.2f, .index_ms = 8, .floor = 0.25f, .gate = 0.7f, .drive = 1.3f, .kick_len = 0.9f, .kick_bright = 1.3f,
     .hat_ms = 8, .open_ms = 35, .hat_hp = 0.90f, .clap_hz = 1700, .ride = 1,
     .ev_density = 14, .ev_low = 10, .ev_mid = 35, .ev_runs = 50, .brk = 4,
     .line = 0.8f, .line_dens = 0.55f, .line_oct = 36, .line_range = 12, .line_chrom = 20,
     .line_ratio = 3.0f, .line_index = 2.2f, .line_ms = 12, .line_gate = 0.4f},
};

const char *ps_world_name(int w) { return W[(unsigned)w % PS_NWORLD].name; }

/* the parameters */
static const ps_param_t PAR[P_NPARAMS] = {
    {"ENERGY", 0, 100, 50}, {"DETAIL", 0, 100, 50}, {"DARK", 0, 100, 50}, {"DRIFT", 0, 100, 50},
    {"TEMPO", -20, 20, 0}, {"ROOT", -5, 6, 0}, {"FILLS", 0, 4, 3}, {"SIT OUT", 0, 100, 25},
    {"LEVEL", 0, 100, 80}, {"TUNE", -6, 6, 0}, {"LENGTH", 0, 100, 50}, {"PUNCH", 0, 100, 50},
    {"LEVEL", 0, 100, 80}, {"BITE", 0, 100, 50}, {"LENGTH", 0, 100, 50}, {"DRIVE", 0, 100, 50},
    {"LEVEL", 0, 100, 80}, {"HATS", 0, 100, 50}, {"TONE", 0, 100, 50}, {"CLAP", 0, 100, 50},
    {"LEVEL", 0, 100, 80}, {"DENSITY", 0, 100, 50}, {"RUNS", 0, 100, 50}, {"ECHO", 0, 100, 50},
    {"LEVEL", 0, 100, 50}, {"FEEDBACK", 0, 90, 42}, {"TONE", 0, 100, 50}, {"TIME", 0, 3, 0},
    {"LEVEL", 0, 100, 70}, {"COLOUR", 0, 100, 50}, {"AIR", 0, 100, 50}, {"WASH", 0, 100, 50},
    {"LEVEL", 0, 100, 70}, {"DENSITY", 0, 100, 50}, {"LENGTH", 0, 100, 50}, {"MUTATE", 0, 100, 50},
};
static volatile int16_t par[P_NPARAMS];
const ps_param_t *ps_param(int p) { return &PAR[(unsigned)p % P_NPARAMS]; }
int ps_get(int p) { return par[(unsigned)p % P_NPARAMS]; }
void ps_set(int p, int v)
{
    const ps_param_t *d = ps_param(p);
    par[(unsigned)p % P_NPARAMS] = (int16_t)(v < d->lo ? d->lo : v > d->hi ? d->hi : v);
}
static const uint8_t ECHO_TICKS[4] = {18, 12, 16, 9};   /* a dotted eighth, an eighth, a triplet quarter, a dotted sixteenth */
static const char *const ECHO_TIME_NAME[4] = {"3/16", "1/8", "1/4T", "3/32"};
const char *ps_echo_time_name(int v) { return ECHO_TIME_NAME[(unsigned)v % 4u]; }

ps_ui_t ps_ui;

static volatile int want_world, want_play;
static volatile uint32_t restart_n;    /* restarts asked for, counted */
static uint32_t restart_seen;
static volatile float ext_bpm;         /* a MIDI clock's tempo (0: the world's) */
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
    float lp;                          /* the bass's one-pole low-pass */
    float drift;                       /* the slow movement: 0..1 over 16 bars */
    /* the percussion: each a hit's age (s, < 0 silent) and level */
    float h_t, h_v, o_t, o_v, c_t, c_v, r_t, r_v;
    float h_hp1, h_hp2, h_x1, h_x2;    /* the hats' two one-pole high-passes (shared by closed and open) */
    float c_low, c_band;               /* the clap's band-pass (state variable) */
    float r_ph[3];                     /* the ride's partials */
    uint32_t rng;
    uint8_t out;                       /* the part sitting out this phrase: 0 none, 1 hats, 2 open, 3 clap, 4 ride */
    uint8_t pat;                       /* the bass's shape this phrase: 0 K-B-B-B, 1 a gallop (K-.-B-B), 2 triplets */
    /* the echo: ping-pong */
    float e_lp[2];
    /* the sections */
    uint8_t sec, colour, drops;
    uint32_t origin;                   /* the bar the phrases count from: the last drop's */
    uint32_t last_drop;
    uint32_t sec_bar, sec_len;         /* bars into the section; a break's length (0: until cued) */
    uint32_t cue_seen, drop_seen;
    uint8_t fill_beat;                 /* this beat a fill (cued by a black key) */
    float part_g[NPART];               /* the parts' gains, gliding to their mutes */
    float x_t;                         /* the crash's age (s, < 0 silent) */
    float z_g, z_low, z_band;          /* the riser: its level (glides), its band-pass */
    /* the atmos */
    float a_lvl;                       /* its level now (glides between the sections) */
    float a_ph[3][2], a_mph[3];        /* the chord's notes, left and right; each note's modulator */
    float a_lfo, a_lp[2];              /* the slow wander; the chord's low-pass */
    float a_low, a_band;               /* the air's band-pass */
} s;
static volatile uint32_t cue_n, drop_n;   /* the UI's cues, counted: the render acts on a change */
static volatile int auto_on = 1;       /* breaks on their own */
static volatile int key_oct;           /* the keys' events: octaves up or down */
static volatile uint8_t line_new;      /* the line drawn anew at the next bar */

/* the macros, -1..1 from their middle (Drift 0..1), and what the parameters make of the world: set as a block
 * begins (mods()) */
static struct {
    float energy, detail, dark, drift;
    int root;
    float bpm;
} m;

/* what changes slowly, worked out as a block begins (coefs()) rather than every sample */
static struct {
    float kick_f0;                     /* the kick's resting pitch, Hz */
    float kick_bright;                 /* the world's punch */
    float clap_f, z_f, air_f;          /* the clap's, the riser's and the atmos's air's band-passes */
    float a_inc[3];                    /* the atmos's notes, turns a sample */
} cf;

#define ECHO_N 16384u                  /* (a dotted eighth at 125 bpm, the slowest a world can be moved to: 15876) */
static PLAT_POOL int16_t echo_buf[2][ECHO_N];
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

static uint32_t rnd(void)
{
    s.rng ^= s.rng << 13;
    s.rng ^= s.rng >> 17;
    s.rng ^= s.rng << 5;
    return s.rng;
}
static float note_hz(int note) { return 440.0f * fm_exp2f((float)(note - 69) / 12.0f); }
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* what events.c asks of the world, as the macros and the EVENTS page lean it */
static int ev_density_now(const world_t *w)
{
    float k = (float)par[P_EV_DENSITY] / 50.0f * (1.0f + 0.6f * m.energy) * (s.sec == SEC_BREAK ? 0.5f : 1.0f);
    return clampi((int)((float)w->ev_density * k + 0.5f), 0, 24);
}
static int ev_runs_now(const world_t *w)
{
    float k = (float)par[P_EV_RUNS] / 50.0f * (1.0f + m.detail);
    return clampi((int)((float)w->ev_runs * k + 0.5f), 0, 100);
}
static int ev_low_now(const world_t *w) { return clampi((int)w->ev_low + (int)(25.0f * m.dark), 0, 100); }
static int ev_mid_now(const world_t *w) { return clampi((int)w->ev_mid + (int)(10.0f * m.dark), 0, 100 - ev_low_now(w)); }
static int ev_run_step(void) { return m.detail > 0.4f ? 3 : 6; }   /* sixteenths, or 32nds at high Detail */
static int ev_run_len(void) { return 3 + (m.detail > 0.0f ? (int)(2.0f * m.detail + 0.5f) : 0); }
static int ev_changes(void)            /* recipes changed as a phrase begins: 0..3 */
{
    return clampi((int)(m.drift * 3.0f + (float)(rnd() % 100u) / 100.0f), 0, 3);
}
static float ev_dark(void) { return m.dark; }

#include "events.c"
#include "fx.c"
#include "line.c"

/* the queue's commands: bits 16.. the kind (0 a key, 1 an effect, 2 a mute, 3 out, 4 a fill) */
enum { CMD_KEY, CMD_FX, CMD_MUTE, CMD_OUT, CMD_FILL, CMD_COLOUR };
void ps_colour(int c) { cmd_post((uint32_t)CMD_COLOUR << 16 | (uint32_t)(c & 3)); }
int ps_colour_now(void) { return s.colour; }
void ps_line_new(void) { line_new = 1; }
void ps_octave(int o) { key_oct = o < -1 ? -1 : o > 2 ? 2 : o; }
void ps_auto(int on) { auto_on = on != 0; }
void ps_fx(int id, int on) { cmd_post((uint32_t)CMD_FX << 16 | (uint32_t)(id & 31) | (uint32_t)(on != 0) << 5); }
const char *ps_fx_name(int id) { return FX_NAME[(unsigned)id % FX_N]; }
void ps_mute(int part) { cmd_post((uint32_t)CMD_MUTE << 16 | (uint32_t)(part & 7)); }
void ps_out(int part, int on) { cmd_post((uint32_t)CMD_OUT << 16 | (uint32_t)(part & 7) | (uint32_t)(on != 0) << 5); }
void ps_fill(void) { cmd_post((uint32_t)CMD_FILL << 16); }

static void cmd_do(uint32_t c)         /* (render, as a block begins) */
{
    uint32_t a = c & 31u, b = c >> 5 & 1u;
    switch (c >> 16) {
    case CMD_KEY:
        ev_key((int)(c & 31u), (int)(c >> 5 & 3u), (int)(c >> 8 & 127u));
        break;
    case CMD_FX:
        fx_cmd((int)a, (int)b);
        break;
    case CMD_MUTE:
        fx.muted ^= (uint8_t)(1u << a);
        ps_ui.muted = fx.muted;
        break;
    case CMD_OUT:
        fx.out = (uint8_t)(b ? fx.out | 1u << a : fx.out & ~(1u << a));
        break;
    case CMD_FILL:
        fx.fill_req = 1;
        break;
    case CMD_COLOUR:
        s.colour = (uint8_t)a;
        break;
    }
}

void ps_key(int k, int down, int value)
{
    if (k < 0 || k >= 27)
        return;
    if (down == 2) {                   /* pressure: straight into the events' table (a byte: no queue to fill) */
        ev.press[k] = (uint8_t)value;
        return;
    }
    if (down)
        ev.press[k] = 0;
    cmd_post((uint32_t)(k & 31) | (uint32_t)(down & 3) << 5 | (uint32_t)(value & 127) << 8);
}

static int fill_now(uint32_t bar, uint32_t beat);

/* the bar now as it will play (psy.h): read by the UI while the render moves on (a frame's glance, no lock) */
int ps_lane(int lane, uint8_t out[24])
{
    const world_t *w = &W[world];
    uint32_t tick = s.tick ? s.tick - 1u : 0u, bar = tick / TPBAR, rb = bar - s.origin, i;
    int groove = s.sec == SEC_MAIN, n = 16;
    for (i = 0; i < 24u; i++)
        out[i] = 0;
    if (!playing)
        return 16;
    switch (lane) {
    case LANE_KICK:
        if (groove)
            for (i = 0; i < 16u; i += 4u)
                out[i] = 3;
        if (groove && par[P_FILLS] && rb % 32u == 31u)
            out[12] = 0;
        break;
    case LANE_BASS:
        if (!groove)
            break;
        if (s.pat == 2u) {             /* triplets: six a beat, the kick on the first */
            n = 24;
            for (i = 0; i < 24u; i++)
                out[i] = i % 6u ? 2 : 0;
        } else
            for (i = 0; i < 16u; i++)
                out[i] = i % 4u == 0u || (s.pat == 1u && i % 4u == 1u) ? 0 : 2;
        if (fill_now(rb, 3))
            for (i = (uint32_t)n * 3u / 4u; i < (uint32_t)n; i++)
                out[i] = 0;
        break;
    case LANE_PERC:
        if (s.sec == SEC_BUILD) {      /* the roll: quarters, eighths, sixteenths; the breath */
            uint32_t every = s.sec_bar == 0u ? 4u : s.sec_bar == 1u ? 2u : 1u;
            for (i = 0; i < 16u; i += every)
                out[i] = (uint8_t)(1u + (s.sec_bar >= 2u) + (s.sec_bar >= 3u));
            if (s.sec_bar >= 3u)
                out[12] = out[13] = out[14] = out[15] = 0;
        } else if (groove) {
            static const uint8_t HAT[4] = {1, 2, 0, 2};
            for (i = 0; i < 16u; i++) {
                uint32_t j = i % 4u;
                if (j != 2u && s.out != 1u && par[P_HATS] > 0)
                    out[i] = HAT[j];
                if (j == 2u && bar >= 8u && s.out != 2u && par[P_HATS] > 0)
                    out[i] = 3;
                if ((i == 4u || i == 12u) && bar >= 16u && s.out != 3u)
                    out[i] = 3;
            }
            if (fill_now(rb, 3))
                for (i = 12; i < 16u; i++)
                    out[i] = (uint8_t)(1u + (i - 12u) / 2u + (i == 15u));
        }
        break;
    case LANE_LINE:
        if (ln.playing)
            for (i = 0; i < 16u; i++) {
                uint32_t k = (rb % 2u) * 16u + i;
                if (ln.on[k])
                    out[i] = (uint8_t)(1 + clampi(ln.note[k] + 2, 0, 30));
            }
        (void)w;
        break;
    case LANE_EVENTS:
        if ((bar >= 4u || s.drops) && s.sec != SEC_BUILD)
            for (i = 0; i < 16u; i++) {
                uint32_t sl = ev.at[(rb % 4u) * 16u + i];
                if (sl < ev.density)
                    out[i] = 2;
            }
        break;
    }
    return n;
}
int ps_line_range(void) { return W[world].line_range + 8; }   /* (+2 below the register, Dark's +6 above) */

void ps_init(void)
{
    int k;
    want_world = world = 2;            /* COLD SIGNAL */
    want_play = playing = 1;
    for (k = 0; k < P_NPARAMS; k++)
        par[k] = PAR[k].def;
    s.k_t = s.b_t = -1.0f;
    s.h_t = s.o_t = s.c_t = s.r_t = -1.0f;
    s.rng = 0x9E3779B9u;
    for (k = 0; k < EV_NVOICE; k++)
        ev.v[k].t = -1.0f;
    ln.t = -1.0f;
    ev.last_type = -1;
    ps_ui.thrown_type = -1;
    s.tick = 0;
    s.next = 0.0f;
}

void ps_world(int w) { want_world = (int)((unsigned)w % PS_NWORLD); }
void ps_play(int on) { want_play = on != 0; }
void ps_restart(void) { restart_n++; }
void ps_ext_bpm(float bpm) { ext_bpm = bpm; }
int ps_playing(void) { return playing; }
int ps_bar(void) { return (int)(s.tick / TPBAR); }
int ps_bpm(void) { return ext_bpm > 0.0f ? (int)(ext_bpm + 0.5f) : (int)(W[want_world].bpm + (float)par[P_TEMPO]); }
int ps_root(void) { return W[want_world].root + par[P_ROOT]; }
void ps_cue(int how)
{
    if (how == 2)
        drop_n++;
    else
        cue_n++;
}
int ps_section(void) { return s.sec; }
int ps_stat(int which) { return which == 0 ? (int)ev.fired : which == 1 ? ev.nmem : ev.last_type; }
const char *ps_event_name(int type) { return EV_NAME[(unsigned)type % EV_NTYPE]; }

static void mods(const world_t *w)     /* the macros and the moved tempo and root, as a block begins */
{
    m.energy = (float)(par[P_ENERGY] - 50) / 50.0f;
    m.detail = (float)(par[P_DETAIL] - 50) / 50.0f;
    m.dark = (float)(par[P_DARK] - 50) / 50.0f;
    m.drift = (float)par[P_DRIFT] / 100.0f;
    m.root = w->root + par[P_ROOT];
    m.bpm = ext_bpm > 0.0f ? ext_bpm : w->bpm + (float)par[P_TEMPO];
}

/* the build's roll: quarters, eighths, then sixteenths, louder bar by bar; its last beat silent */
static void build_step(uint32_t t, uint32_t beat)
{
    uint32_t b = s.sec_bar, every = b == 0u ? 24u : b == 1u ? 12u : 6u;
    if ((b >= 3u && beat == 3u) || t % every)
        return;
    s.c_t = 0.0f;
    s.c_v = 0.35f + 0.65f * (float)(b * 4u + beat) / 15.0f;
    ps_ui.hits[HIT_PERC]++;
}

/* bar: since PLAY (the parts come in over the first bars); rb: since the last drop (the phrases) */
static void perc_step(const world_t *w, uint32_t t, uint32_t beat, uint32_t bar, uint32_t rb, int fill)
{
    uint32_t six = t / 6u;             /* the sixteenth in the beat */
    static const float HAT_V[4] = {0.55f, 0.80f, 0.0f, 1.0f};
    float hats = (float)par[P_HATS] / 50.0f;
    if (s.sec == SEC_BUILD) {
        build_step(t, beat);
        return;
    }
    if (s.sec == SEC_BREAK)
        return;
    if (t % 6u) {                      /* between sixteenths: at high Detail, a 32nd flam on the hats now and then */
        if (t % 3u == 0u && six != 2u && m.detail > 0.3f && s.out != 1u && !fill &&
            (float)(rnd() % 100u) < 60.0f * (m.detail - 0.3f)) {
            s.h_t = 0.0f;
            s.h_v = 0.35f * hats;
            ps_ui.hits[HIT_PERC]++;
        }
        return;
    }
    if (t == 0u && beat == 0u && rb % 8u == 0u) {    /* a phrase begins: who sits it out */
        float p = (float)par[P_SITOUT] * (1.0f - 0.7f * m.energy);
        s.out = 0;
        if (bar >= 32u && rb > 0u && (float)(rnd() % 100u) < p)   /* (not on a drop's first phrase) */
            s.out = (uint8_t)(1u + rnd() % (w->ride ? 4u : 3u));
        ps_ui.sitting_out = (int8_t)s.out;
    }
    if (fill) {                        /* the fill's beat: a clap roll, rising */
        s.c_t = 0.0f;
        s.c_v = 0.45f + 0.18f * (float)six;
        ps_ui.hits[HIT_PERC]++;
        return;
    }
    if (six != 2u && s.out != 1u && hats > 0.0f) {
        int weak = six == 0u;          /* (on the kick) */
        if (!(weak && m.energy < 0.0f && (float)(rnd() % 100u) < -70.0f * m.energy)) {   /* low Energy: fewer */
            s.h_t = 0.0f;
            s.h_v = HAT_V[six] * hats * (0.85f + 0.15f * (float)(rnd() % 100u) / 100.0f);
            ps_ui.hits[HIT_PERC]++;
        }
    }
    if (six == 2u && bar >= 8u && s.out != 2u && hats > 0.0f) {
        s.o_t = 0.0f;
        s.o_v = hats > 1.0f ? 1.0f : hats;
        ps_ui.hits[HIT_PERC]++;
    }
    if (six == 0u && (beat == 1u || beat == 3u) && bar >= 16u && s.out != 3u) {
        s.c_t = 0.0f;
        s.c_v = (float)par[P_CLAP] / 50.0f;
        ps_ui.hits[HIT_PERC]++;
    }
    if (w->ride && six % 2u == 0u && bar >= 24u && s.out != 4u) {
        s.r_t = 0.0f;
        s.r_v = six == 2u ? 1.0f : 0.6f;
    }
}

static int fill_now(uint32_t bar, uint32_t beat)   /* this beat a fill's (the bass rests) */
{
    static const uint8_t EVERY[5] = {0, 32, 16, 8, 4};
    uint32_t e = EVERY[clampi(par[P_FILLS], 0, 4)];
    return e && bar % e == e - 1u && beat == 3u;
}

static void enter(int sec, uint32_t len)
{
    s.sec = (uint8_t)sec;
    s.sec_bar = 0;
    s.sec_len = len;
}

static void drop(uint32_t bar)         /* on this bar's first tick: the groove again, a shade other */
{
    enter(SEC_MAIN, 0);
    s.origin = s.last_drop = bar;
    s.drops++;
    s.colour = (uint8_t)((s.colour + 1u + rnd() % 3u) % 4u);   /* another of the bass's four colours */
    s.out = 0;
    ps_ui.sitting_out = 0;
    s.x_t = 0.0f;                      /* the crash */
    ps_ui.hits[HIT_PERC]++;
}

static void section_bar(const world_t *w, uint32_t bar)   /* a bar begins: the cues, and what comes on its own */
{
    uint32_t cue = cue_n, dn = drop_n;
    int next = cue != s.cue_seen, now = dn != s.drop_seen;
    s.cue_seen = cue;
    s.drop_seen = dn;
    s.sec_bar++;
    if (now) {
        drop(bar);
        return;
    }
    if (s.sec == SEC_MAIN) {
        uint32_t rb = bar - s.origin;
        if (next)
            enter(SEC_BREAK, 0);
        else if (auto_on && rb && rb % 32u == 0u && bar - s.last_drop >= 64u && (float)(rnd() % 100u) < 70.0f * m.drift)
            enter(SEC_BREAK, w->brk);
    } else if (s.sec == SEC_BREAK) {
        if (next || (s.sec_len && s.sec_bar >= s.sec_len))
            enter(SEC_BUILD, 4);
    } else if (s.sec_bar >= 4u) {
        drop(bar);
    }
}

static void step(const world_t *w)     /* a tick begins: what strikes on it */
{
    uint32_t t = s.tick % TPB, beat = s.tick / TPB % 4u, bar = s.tick / TPBAR, rb;
    int groove, bass_rests, kick_rests, bass;
    if (s.tick % TPBAR == 0u)
        section_bar(w, bar);
    if (t % 6u == 0u)
        fx_sixteenth();
    if (t == 0u) {                     /* a fill cued (a black key): this beat */
        s.fill_beat = fx.fill_req;
        fx.fill_req = 0;
    }
    rb = bar - s.origin;
    groove = s.sec == SEC_MAIN;
    bass_rests = !groove || fill_now(rb, beat) || s.fill_beat;
    kick_rests = !groove || (par[P_FILLS] && rb % 32u == 31u && beat == 3u);   /* every 32nd bar's: the kick too */
    perc_step(w, t, beat, bar, rb, groove && bass_rests);
    ev_step(w, t, rb, (uint32_t)((s.tick - s.origin * TPBAR) / 6u), (bar < 4u && !s.drops) || s.sec == SEC_BUILD);
    ln_step(w, t, bar, rb, (uint32_t)((s.tick - s.origin * TPBAR) / 6u));
    if (t == 0u && beat == 0u && rb % 8u == 0u && groove) {   /* a phrase: the bass's shape, now and then another */
        uint32_t r = rnd() % 100u;
        s.pat = (uint8_t)(rb == 0u || r >= (uint32_t)(w->trip + w->gallop) ? 0 : r < w->trip ? 2 : 1);
    }
    ps_ui.section = (int8_t)s.sec;
    ps_ui.sec_bar = (uint8_t)(groove ? rb % 8u : s.sec_bar);
    ps_ui.drops = s.drops;
    ps_ui.cued = (int8_t)(drop_n != s.drop_seen ? 2 : cue_n != s.cue_seen ? 1 : 0);
    if (t == 0u) {
        if (!kick_rests) {
            s.k_t = 0.0f;
            s.k_ph = 0.0f;
            ps_ui.hits[HIT_KICK]++;
        }
        return;
    }
    {
        uint32_t roll = s.pat == 2u ? 4u : 6u;   /* triplets: K-b-b-b-b-b; a gallop: K-.-B-B */
        bass = t % roll == 0u && !(s.pat == 1u && t == 6u);
        if (!bass || bass_rests)
            return;
        if (w->skip && rnd() % 100u < w->skip)   /* a stutter: this one rests */
            return;
        s.b_t = 0.0f;
        s.b_cph = s.b_mph = 0.0f;      /* every note from the same place: the roll is tight */
        s.b_gate = w->gate * (0.6f + 0.8f * (float)par[P_BASS_LEN] / 100.0f) * s.tick_len * (float)roll / PS_SR;
        if (s.b_gate > 0.95f * s.tick_len * (float)roll / PS_SR)
            s.b_gate = 0.95f * s.tick_len * (float)roll / PS_SR;
        s.b_inc = note_hz(m.root + (w->jump && rnd() % 100u < w->jump ? ((rnd() & 1u) ? 12 : 7) : 0)) / PS_SR;
    }
    ps_ui.hits[HIT_BASS]++;
}

static float kick(float dt)
{
    float f0, f, env, x, t = s.k_t, punch = (float)par[P_KICK_PUNCH] / 50.0f * cf.kick_bright;
    if (t < 0.0f)
        return 0.0f;
    if (t >= s.k_len) {
        s.k_t = -1.0f;
        return 0.0f;
    }
    f0 = cf.kick_f0;                                /* tuned to the key */
    f = f0 + f0 * (4.0f + 5.0f * punch) * fm_expf(-t / 0.011f);   /* the pitch falls: Punch, from how high */
    s.k_ph += f * dt;
    if (s.k_ph >= 1.0f)
        s.k_ph -= 1.0f;
    env = fm_expf(-t / (s.k_len * 0.45f));
    env *= 1.0f - t / s.k_len;                      /* to nothing by its end: the bass's place stays clear */
    x = fm_sin_turns(s.k_ph) * env;
    x += (t < 0.002f ? 0.35f * punch * (1.0f - t / 0.002f) : 0.0f) * fm_sin_turns(s.k_ph * 7.0f);   /* the click */
    s.k_t = t + dt;
    return fm_tanhf(x * 1.8f);
}

static const float COLOUR[4][2] = {{1.0f, 1.0f}, {1.0f, 1.35f}, {1.5f, 0.8f}, {0.5f, 1.25f}};   /* ratio, bite */

static float bass(const world_t *w, float dt)
{
    float t = s.b_t, idx, env, x, mo, bite = (float)par[P_BASS_BITE] / 50.0f * COLOUR[s.colour][1];
    float depth = 0.5f * m.drift * 2.0f;           /* the slow movement's depth: Drift (50: as the world has it) */
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
    idx = w->index * bite * (1.0f - 0.5f * depth + depth * s.drift);   /* the slow movement opens and closes it */
    idx = idx * fm_expf(-t * 1000.0f / w->index_ms) + w->floor * bite;
    mo = fm_sin_turns(s.b_mph);
    x = fm_sin_turns(s.b_cph + idx * mo * 0.159155f) * env;
    s.b_cph += s.b_inc;
    if (s.b_cph >= 1.0f)
        s.b_cph -= 1.0f;
    s.b_mph += s.b_inc * w->ratio * COLOUR[s.colour][0];
    if (s.b_mph >= 1.0f)
        s.b_mph -= 1.0f;
    if (s.b_t >= 0.0f)
        s.b_t = t + dt;
    x = fm_tanhf(x * w->drive * (0.5f + (float)par[P_BASS_DRIVE] / 100.0f));
    s.lp += (x - s.lp) * fm_clampf(0.45f - 0.2f * m.dark, 0.2f, 0.7f);   /* Dark: darker */
    return s.lp;
}

static float noise(void) { return (float)(int32_t)rnd() * (1.0f / 2147483648.0f); }

/* the percussion, a sample: l and r added to */
static void perc(const world_t *w, float dt, float *l, float *r)
{
    float n = noise(), hp, x, a = fm_clampf(w->hat_hp + 0.002f * (float)(par[P_HAT_TONE] - 50) - 0.05f * m.dark, 0.5f, 0.97f);
    /* the hats: noise through two high-passes */
    s.h_hp1 = a * (s.h_hp1 + n - s.h_x1);
    s.h_x1 = n;
    s.h_hp2 = a * (s.h_hp2 + s.h_hp1 - s.h_x2);
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
        float t = s.c_t, f = cf.clap_f, hi, env;
        (void)w;
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
        float mo = 0.0f, env = fm_expf(-s.r_t / 0.16f);
        int i;
        for (i = 0; i < 3; i++) {
            s.r_ph[i] += RH[i] * dt;
            if (s.r_ph[i] >= 1.0f)
                s.r_ph[i] -= 1.0f;
            mo += fm_sin_turns(s.r_ph[i]);
        }
        x = (0.35f * mo + 0.6f * hp) * env * s.r_v * 0.05f;
        *l += 1.15f * x;
        *r += 0.75f * x;
        if ((s.r_t += dt) > 0.9f)
            s.r_t = -1.0f;
    }
    /* the drop's crash: the hats' noise, long */
    if (s.x_t >= 0.0f) {
        x = hp * fm_expf(-s.x_t / 0.6f) * 0.30f;
        *l += 0.9f * x;
        *r += 1.1f * x;
        if ((s.x_t += dt) > 3.0f)
            s.x_t = -1.0f;
    }
}

/* the build's riser: a band of noise rising over its four bars, gone for the breath; returns what goes to the echo */
static float build_progress(void)      /* 0..1 through a build's four bars */
{
    return s.sec == SEC_BUILD ? ((float)s.sec_bar + (float)(s.tick % TPBAR) / (float)TPBAR) / 4.0f : 0.0f;
}

static float riser(float n, float *l, float *r)
{
    float p = build_progress(), target = 0.0f, f, x;
    if (s.sec == SEC_BUILD && !(s.sec_bar >= 3u && s.tick % TPBAR >= 3u * TPB))
        target = p * p;
    s.z_g += (target - s.z_g) * (target > s.z_g ? 0.0005f : 0.004f);
    if (s.z_g < 0.0003f)
        return 0.0f;
    f = cf.z_f;
    s.z_band += f * (n - s.z_low - 0.5f * s.z_band);
    s.z_low += f * s.z_band;
    x = s.z_band * s.z_g * 0.35f;
    *l += x;
    *r += x;
    return x;
}

/* the atmos, a sample: l and r added to; returns what goes to the echo (Wash) */
static float atmos(const world_t *w, float n, float *l, float *r)
{
    float lvl = (float)par[P_ATMOS_LEVEL] / 70.0f, target = (s.sec == SEC_MAIN ? w->atmos : 1.0f) * lvl;
    float colour = (float)par[P_ATMOS_COLOUR] / 100.0f, air = (float)par[P_ATMOS_AIR] / 100.0f, wob, al = 0.0f, ar = 0.0f, k, x;
    int i;
    s.a_lvl += (target - s.a_lvl) * (target > s.a_lvl ? 1.0f / (2.5f * PS_SR) : 1.0f / (1.5f * PS_SR));
    if (s.a_lvl < 0.0005f)
        return 0.0f;
    colour += 0.4f * build_progress(); /* it opens as the build rises */
    if ((s.a_lfo += 0.05f / PS_SR) >= 1.0f)
        s.a_lfo -= 1.0f;
    wob = fm_sin_turns(s.a_lfo);
    for (i = 0; i < 3; i++) {          /* the chord: each note a little apart left and right, its modulator shared */
        float inc = cf.a_inc[i], idx = (0.15f + 1.2f * colour) * (0.75f + 0.25f * wob), mo;
        mo = fm_sin_turns(s.a_mph[i]) * idx * 0.159155f;
        al += fm_sin_turns(s.a_ph[i][0] + mo);
        ar += fm_sin_turns(s.a_ph[i][1] + mo);
        if ((s.a_mph[i] += 2.0f * inc) >= 1.0f)
            s.a_mph[i] -= 1.0f;
        if ((s.a_ph[i][0] += inc * (1.0f - 0.0016f * (float)(i + 1))) >= 1.0f)
            s.a_ph[i][0] -= 1.0f;
        if ((s.a_ph[i][1] += inc * (1.0f + 0.0016f * (float)(i + 1))) >= 1.0f)
            s.a_ph[i][1] -= 1.0f;
    }
    k = fm_clampf(0.03f + 0.35f * colour, 0.02f, 0.6f);
    s.a_lp[0] += (al - s.a_lp[0]) * k;
    s.a_lp[1] += (ar - s.a_lp[1]) * k;
    {   /* the air: a band of noise, wandering, swaying left and right */
        float f = cf.air_f;
        s.a_band += f * (n - s.a_low - 0.7f * s.a_band);
        s.a_low += f * s.a_band;
        x = s.a_band * air * 0.5f;
    }
    al = s.a_lp[0] * 0.10f + x * (0.6f + 0.35f * wob);
    ar = s.a_lp[1] * 0.10f + x * (0.6f - 0.35f * wob);
    *l += al * s.a_lvl;
    *r += ar * s.a_lvl;
    return (al + ar) * s.a_lvl * (float)par[P_ATMOS_WASH] / 100.0f;
}

static void coefs(const world_t *w)    /* as a block begins: what changes slowly */
{
    static const int8_t IV[2][3] = {{24, 31, 36}, {24, 30, 25}};   /* the atmos over the root: fifths; dark, a tritone and a second */
    float colour = (float)par[P_ATMOS_COLOUR] / 100.0f + 0.4f * build_progress();
    int i;
    cf.kick_f0 = note_hz(m.root + par[P_KICK_TUNE]);
    cf.kick_bright = w->kick_bright;
    cf.clap_f = 2.0f * fm_sinf(3.14159265f * w->clap_hz / PS_SR);
    cf.z_f = 2.0f * fm_sinf(3.14159265f * 300.0f * fm_exp2f(4.5f * build_progress()) / PS_SR);   /* 300 Hz to 6.8 kHz */
    cf.air_f = 2.0f * fm_sinf(3.14159265f * (1200.0f + 900.0f * fm_sin_turns(s.a_lfo) + 1500.0f * colour) / PS_SR);
    for (i = 0; i < 3; i++)
        cf.a_inc[i] = note_hz(m.root + IV[m.dark > 0.3f][i]) / PS_SR;
    fx_block(s.tick_len);
}

static void tempo(const world_t *w)
{
    mods(w);
    s.tick_len = PS_SR * 60.0f / m.bpm / (float)TPB;
    s.k_len = w->kick_len * (0.7f + 0.6f * (float)par[P_KICK_LEN] / 100.0f) * s.tick_len * 6.0f / PS_SR;
    if (s.k_len > 0.98f * s.tick_len * 4.0f / PS_SR)   /* never into the bass's first note (a triplet's, the soonest) */
        s.k_len = 0.98f * s.tick_len * 4.0f / PS_SR;
    coefs(w);
}

void ps_render(float *outl, float *outr, uint32_t n)
{
    const world_t *w = &W[world];
    float dt = 1.0f / PS_SR;
    float kick_g = 0.85f * (float)par[P_KICK_LEVEL] / 80.0f, bass_g = 0.55f * (float)par[P_BASS_LEVEL] / 80.0f;
    float perc_g = (float)par[P_PERC_LEVEL] / 80.0f, ev_g = (float)par[P_EV_LEVEL] / 80.0f;
    float line_g = (float)par[P_LINE_LEVEL] / 70.0f;
    float send_g = (float)par[P_EV_ECHO] / 50.0f, ret_g = 0.7f * (float)par[P_ECHO_LEVEL] / 50.0f;
    float fb = (float)par[P_ECHO_FB] / 100.0f, tone = 0.12f + 0.46f * (float)par[P_ECHO_TONE] / 100.0f;
    uint32_t i;
    static float fx_send;              /* what the effects sent to the echo (the next frame takes it) */
    if (want_play != playing || restart_n != restart_seen) {
        restart_seen = restart_n;
        playing = want_play;
        s.tick = 0;
        s.next = 0.0f;
        enter(SEC_MAIN, 0);            /* from the top: the groove, the phrases from bar 0, the cues forgotten */
        s.origin = s.last_drop = 0;
        s.cue_seen = cue_n;
        s.drop_seen = drop_n;
    }
    while (cmd_r != cmd_w) {           /* the keys, the effects, the parts */
        uint32_t c = cmd_q[cmd_r % CMD_N];
        cmd_r++;
        cmd_do(c);
    }
    tempo(w);
    if (fx.held >> FX_WASH & 1u) {     /* WASH: the echo longer and darker; FREEZE: it keeps what it holds */
        fb = 0.86f;
        tone = 0.14f;
    }
    if (fx.held >> FX_FREEZE & 1u) {
        fb = 1.0f;
        tone = 1.0f;
        send_g = 0.0f;
    }
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
                ps_ui.tick = s.tick;
                s.next += s.tick_len;
            }
            s.next -= 1.0f;
        }
        {
            float s_kick = kick(dt), s_bass = bass(w, dt), c, l = 0.0f, r = 0.0f, el = 0.0f, er = 0.0f, send, xl, xr;
            float al = 0.0f, ar = 0.0f, ll = 0.0f, lr = 0.0f, n = noise(), *pg = s.part_g, ml, mr;
            uint32_t d = (uint32_t)(s.tick_len * (float)ECHO_TICKS[par[P_ECHO_TIME] & 3]);
            int k;
            for (k = 0; k < NPART; k++)        /* the parts' gains glide to their mutes (~3 ms) */
                pg[k] += (((fx.muted | fx.out) >> k & 1u ? 0.0f : 1.0f) - pg[k]) * 0.008f;
            c = kick_g * pg[PART_KICK] * s_kick + bass_g * pg[PART_BASS] * s_bass;
            perc(w, dt, &l, &r);
            l *= pg[PART_PERC];
            r *= pg[PART_PERC];
            send = ev_render(dt, &el, &er) * ev_g * send_g * pg[PART_EVENTS];
            el *= pg[PART_EVENTS];
            er *= pg[PART_EVENTS];
            {   /* the line: softer in a break, and more of it into the echo */
                float lg = line_g * pg[PART_LINE] * (s.sec == SEC_BREAK ? 0.6f : 1.0f);
                send += ln_render(w, dt, &ll, &lr) * lg * (s.sec == SEC_MAIN ? 0.5f : 1.2f);
                ll *= lg;
                lr *= lg;
            }
            send += perc_g * riser(n, &l, &r) * 0.5f + atmos(w, n, &al, &ar) * pg[PART_ATMOS];
            al *= pg[PART_ATMOS];
            ar *= pg[PART_ATMOS];
            if (d >= ECHO_N)
                d = ECHO_N - 1u;
            xl = (float)echo_buf[0][(echo_w - d) % ECHO_N] * (1.0f / 32768.0f);
            xr = (float)echo_buf[1][(echo_w - d) % ECHO_N] * (1.0f / 32768.0f);
            if (fb < 1.0f)
                send += fx_send;
            else
                send = 0.0f;
            s.e_lp[0] += (xr * fb + send * 0.5f - s.e_lp[0]) * tone;   /* into the left: the right's, and the send */
            s.e_lp[1] += (xl * fb - s.e_lp[1]) * tone;                  /* into the right: the left's */
            echo_buf[0][echo_w % ECHO_N] = (int16_t)(fm_clampf(s.e_lp[0], -0.99f, 0.99f) * 32767.0f);
            echo_buf[1][echo_w % ECHO_N] = (int16_t)(fm_clampf(s.e_lp[1], -0.99f, 0.99f) * 32767.0f);
            echo_w++;
            ml = perc_g * l + ev_g * el + ll + al;     /* above the kick and the bass: THROW and WASH take it */
            mr = perc_g * r + ev_g * er + lr + ar;
            outl[i] = c + ml + ret_g * xl;
            outr[i] = c + mr + ret_g * xr;
            fx_send = fx_render(s.tick_len, &outl[i], &outr[i], ml, mr, n);
        }
    }
}
