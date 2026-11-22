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
 * fewer; its build, a clap roll quickening (quarters, eighths, sixteenths, and an ending: the last beat silent, a
 * breath, or another) under a noise rising, over BUILD's bars (4 unless moved); the drop, on a bar's first beat: a crash, the phrases counted from
 * it, the events' loop drawn anew, and the bass a shade other (another of its colours: its ratio, its bite). A cue
 * (SAVE) acts as the next bar begins; a break on its own comes as a 32-bar block begins (how likely: Drift and
 * BREAKS), GAP bars after the last drop at least, and lasts BREAK's bars or the world's (then builds and drops on its
 * own); a cued one waits for a build's cue.
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
    /* the bass: K-B-B-B, and now and then a phrase of another shape (a gallop, K-.-B-B, may sit better, a little more
     * of it; an offbeat, K-.-B-., too); triplets one bar only, a touch (the user: "there may be some, but rare"; a
     * phrase of them, too much). How often each comes, counted: tools/stats.c */
    uint8_t trip;                      /* % of phrases with a bar of sixteenth triplets (K-b-b-b-b-b): its 4th or 8th */
    uint8_t gallop, off;               /* % of phrases in a gallop, an offbeat */
    uint8_t bk[PS_NBUILD - 1];         /* the builds' leaning (AUTO): ROLL, KICK, FILTER, TRIPLET, STUTTER, POLY, SINK,
                                        * FAKE, HALF, ACID, STAIRS, GATE, out of their sum */
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
    {.name = "SWAMP RADIO", .bpm = 145, .root = 29, .gallop = 20, .off = 15, .bk = {25, 15, 30, 5, 10, 5, 10, 10, 15, 15, 5, 5}, .skip = 9,
     .ratio = 1.0f, .index = 1.8f, .index_ms = 22, .floor = 0.2f, .gate = 0.82f, .drive = 1.1f, .kick_len = 1.0f, .kick_bright = 0.5f,
     .hat_ms = 18, .open_ms = 70, .hat_hp = 0.70f, .clap_hz = 900,
     .ev_density = 6, .ev_low = 35, .ev_mid = 45, .ev_runs = 10, .atmos = 0.8f, .brk = 16,
     .line = 0.1f, .line_dens = 0.25f, .line_oct = 24, .line_range = 7, .line_chrom = 30, .line_slide = 30,
     .line_ratio = 1.0f, .line_index = 1.5f, .line_ms = 60, .line_gate = 0.6f, .line_wah = 0.3f},
    /* deep and long, a thick low bass, a growl now and then, sinking sounds, a dark swell; long breaks */
    {.name = "UNDER THE ROOTS", .bpm = 148, .root = 28, .gallop = 15, .off = 15, .bk = {25, 15, 30, 5, 10, 5, 10, 10, 15, 15, 5, 5},
     .ratio = 0.5f, .index = 2.6f, .index_ms = 18, .floor = 0.3f, .gate = 0.8f, .drive = 1.5f, .kick_len = 1.0f, .kick_bright = 0.7f,
     .hat_ms = 14, .open_ms = 60, .hat_hp = 0.78f, .clap_hz = 1000,
     .ev_density = 4, .ev_low = 55, .ev_mid = 30, .ev_runs = 10, .atmos = 0.55f, .brk = 24,
     .line = 0.35f, .line_dens = 0.3f, .line_oct = 12, .line_range = 5, .line_chrom = 50, .line_slide = 50,
     .line_ratio = 1.5f, .line_index = 3.0f, .line_ms = 120, .line_gate = 0.9f, .line_wah = 0.2f},
    /* cold dark psy: hard, short sixteenths, a short phrase creeping in semitones, lasers, a thin band of noise */
    {.name = "COLD SIGNAL", .bpm = 152, .root = 30, .gallop = 15, .off = 10, .bk = {25, 25, 20, 5, 10, 10, 5, 10, 10, 10, 10, 10},
     .ratio = 1.0f, .index = 3.2f, .index_ms = 14, .floor = 0.35f, .gate = 0.72f, .drive = 1.6f, .kick_len = 0.95f, .kick_bright = 1.0f,
     .hat_ms = 14, .open_ms = 55, .hat_hp = 0.80f, .clap_hz = 1100,
     .ev_density = 5, .ev_low = 30, .ev_mid = 55, .ev_runs = 15, .atmos = 0.2f, .brk = 16,
     .line = 0.6f, .line_dens = 0.35f, .line_oct = 24, .line_range = 4, .line_chrom = 70, .line_slide = 10,
     .line_ratio = 3.0f, .line_index = 2.5f, .line_ms = 25, .line_gate = 0.45f},
    /* bouncing: the bass jumps now and then, a talking FM line, gurgles; sudden turns */
    {.name = "RUBBER CIRCUS", .bpm = 156, .root = 31, .trip = 25, .gallop = 25, .off = 10, .bk = {20, 20, 15, 20, 10, 15, 5, 10, 10, 15, 15, 10}, .jump = 12,
     .ratio = 1.0f, .index = 3.0f, .index_ms = 16, .floor = 0.3f, .gate = 0.6f, .drive = 1.4f, .kick_len = 0.9f, .kick_bright = 1.2f,
     .hat_ms = 12, .open_ms = 50, .hat_hp = 0.82f, .clap_hz = 1300,
     .ev_density = 7, .ev_low = 20, .ev_mid = 60, .ev_runs = 20, .atmos = 0.1f, .brk = 8,
     .line = 0.7f, .line_dens = 0.45f, .line_oct = 24, .line_range = 9, .line_chrom = 20, .line_slide = 35,
     .line_ratio = 1.0f, .line_index = 4.0f, .line_ms = 40, .line_gate = 0.6f, .line_wah = 0.8f},
    /* heavy, a distorted bass, a long dragging phrase, groans; big breaks */
    {.name = "BLACK UNDERTOW", .bpm = 160, .root = 28, .gallop = 15, .off = 10, .bk = {25, 25, 20, 5, 10, 5, 10, 10, 15, 15, 5, 10},
     .ratio = 1.0f, .index = 3.6f, .index_ms = 20, .floor = 0.45f, .gate = 0.75f, .drive = 2.2f, .kick_len = 1.0f, .kick_bright = 1.1f,
     .hat_ms = 14, .open_ms = 55, .hat_hp = 0.78f, .clap_hz = 1000,
     .ev_density = 9, .ev_low = 50, .ev_mid = 35, .ev_runs = 15, .atmos = 0.25f, .brk = 16,
     .line = 0.6f, .line_dens = 0.25f, .line_oct = 12, .line_range = 6, .line_chrom = 50, .line_slide = 60,
     .line_ratio = 2.0f, .line_index = 3.5f, .line_ms = 200, .line_gate = 1.0f, .line_wah = 0.1f},
    /* tight, a short metallic repeat, the staircases begin; now and then a bar of triplets */
    {.name = "COPPER NERVE", .bpm = 168, .root = 29, .trip = 40, .gallop = 15, .off = 5, .bk = {20, 20, 15, 20, 10, 15, 5, 10, 5, 15, 10, 15},
     .ratio = 2.0f, .index = 2.8f, .index_ms = 11, .floor = 0.3f, .gate = 0.72f, .drive = 1.5f, .kick_len = 0.93f, .kick_bright = 1.1f,
     .hat_ms = 11, .open_ms = 45, .hat_hp = 0.85f, .clap_hz = 1300, .ride = 1,
     .ev_density = 8, .ev_low = 20, .ev_mid = 45, .ev_runs = 30, .atmos = 0.1f, .brk = 8,
     .line = 0.7f, .line_dens = 0.4f, .line_oct = 36, .line_range = 5, .line_chrom = 30, .line_slide = 5,
     .line_ratio = 3.5f, .line_index = 2.5f, .line_ms = 18, .line_gate = 0.5f},
    /* bright hi-tech: clean sixteenths, a bright metallic lead, staircases flying left and right; short breaks */
    {.name = "GLASS INSECTS", .bpm = 176, .root = 30, .gallop = 10, .off = 5, .bk = {20, 25, 10, 20, 15, 10, 5, 10, 5, 10, 15, 15},
     .ratio = 2.0f, .index = 2.4f, .index_ms = 9, .floor = 0.25f, .gate = 0.76f, .drive = 1.3f, .kick_len = 0.92f, .kick_bright = 1.2f,
     .hat_ms = 10, .open_ms = 40, .hat_hp = 0.88f, .clap_hz = 1500, .ride = 1,
     .ev_density = 9, .ev_low = 10, .ev_mid = 40, .ev_runs = 35, .atmos = 0.05f, .brk = 8,
     .line = 0.6f, .line_dens = 0.4f, .line_oct = 36, .line_range = 12, .line_slide = 15,
     .line_ratio = 2.0f, .line_index = 2.0f, .line_ms = 30, .line_gate = 0.6f},
    /* fast and fine: the detail in the line's fragments and the events, the densest; changing all the time */
    {.name = "CHROME SWARM", .bpm = 186, .root = 31, .gallop = 5, .off = 5, .bk = {20, 25, 5, 20, 15, 10, 5, 10, 5, 10, 15, 20},
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
    {"LEVEL", 0, 100, 80}, {"TUNE", -6, 6, 0}, {"DRIVE", 0, 100, 50}, {"PUNCH", 0, 100, 50},
    {"LEVEL", 0, 100, 80}, {"BITE", 0, 100, 50}, {"LENGTH", 0, 100, 50}, {"DRIVE", 0, 100, 50},
    {"LEVEL", 0, 100, 80}, {"HATS", 0, 100, 50}, {"TONE", 0, 100, 50}, {"CLAP", 0, 100, 50},
    {"LEVEL", 0, 100, 80}, {"DENSITY", 0, 100, 50}, {"RUNS", 0, 100, 50}, {"ECHO", 0, 100, 50},
    {"LEVEL", 0, 100, 50}, {"FEEDBACK", 0, 90, 42}, {"TONE", 0, 100, 50}, {"TIME", 0, 3, 0},
    {"LEVEL", 0, 100, 70}, {"COLOUR", 0, 100, 50}, {"AIR", 0, 100, 50}, {"WASH", 0, 100, 50},
    {"LEVEL", 0, 100, 70}, {"DENSITY", 0, 100, 50}, {"LENGTH", 0, 100, 50}, {"MUTATE", 0, 100, 50},
    {"BREAKS", 0, 100, 50}, {"LENGTH", 0, 4, 0}, {"LENGTH", 0, 2, 1}, {"GAP", 0, 3, 1},
    {"RANGE", 0, 100, 50}, {"SLIDE", 0, 100, 50}, {"BITE", 0, 100, 50}, {"DECAY", 0, 100, 50},
    {"SEND", 0, 100, 0}, {"SEND", 0, 100, 50},
    {"DECAY", 0, 100, 50}, {"DECAY", 0, 100, 50}, {"OPEN", 0, 100, 50}, {"DRIVE", 0, 100, 0},
    {"PITCH", -12, 12, 0}, {"SPREAD", 0, 100, 50}, {"MOTION", 0, 100, 50}, {"DEPTH", 0, 100, 50},
    {"MORPH", 0, 100, 40},
};
static const uint8_t BREAK_BARS[5] = {0, 4, 8, 16, 32};   /* P_BREAK_LEN (0: the world's) */
static const uint8_t BUILD_BARS[3] = {2, 4, 8};           /* P_BUILD_LEN */
static const uint8_t GAP_BARS[4] = {32, 64, 96, 128};     /* P_GAP */
const char *ps_phase_name(int p, int v)
{
    static const char *const BRK[5] = {"WORLD", "4", "8", "16", "32"}, *const BLD[3] = {"2", "4", "8"},
                             *const GAP[4] = {"32", "64", "96", "128"};
    if (p == P_BREAKS)
        return v <= 0 ? "CUED" : 0;
    if (p == P_BREAK_LEN)
        return BRK[v < 0 ? 0 : v > 4 ? 4 : v];
    if (p == P_BUILD_LEN)
        return BLD[v < 0 ? 0 : v > 2 ? 2 : v];
    if (p == P_GAP)
        return GAP[v < 0 ? 0 : v > 3 ? 3 : v];
    return 0;
}
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
    const float *k_ty;                 /* the kick's type, taken as it strikes (KICK_TYPE) */
    float k_mph;                       /* KNOCK's modulator */
    uint32_t k_nz;                     /* CRACK's noise: its own dice */
    float k_nb, k_nl;                  /* (rounded, and its last sample: the lows taken off) */
    float k_dr, k_cmp;                 /* its drive (the type's, DRIVE's), and the level keeping its peak */
    float h_k, o_k, c_k, r_k;          /* each one's decay, of the world's: its type's, taken as it strikes */
    float h_a, o_a;                    /* the hats' attack (s; SEED's), taken as they strike */
    float p_bead, p_bt;                /* SEED's grain: on or low now and then, smoothed to it */
    float p_sw, p_slow, p_sband;       /* SEED's band: its weight (moving to it), its filter */
    uint32_t p_bc;
    float p_w[4];                      /* the percussion's source, moving to its type's: noise, metal, grit; its level */
    float p_ph[6], p_nh;               /* TIN's six squares; GRIT's noise held, */
    uint32_t p_hc;                     /* its count */
    uint32_t rng;
    uint8_t out;                       /* the part sitting out this phrase: 0 none, 1 hats, 2 open, 3 clap, 4 ride */
    uint8_t pat;                       /* the bass's shape this phrase: 0 K-B-B-B, 1 a gallop (K-.-B-B), 3 an offbeat
                                        * (K-.-B-.); 2 triplets (a bar's: bass_pat()) */
    uint8_t trip_at;                   /* the phrase's bar of triplets (3 or 7; 255: none) */
    uint8_t kit_pat, kit_bars;         /* the kit's shape over the groove's (s.pat's values), its bars to go (0: none) */
    uint8_t shape_was;                 /* GROOVE's ALGORITHM as the last bar began (turned: the kit's shape gone) */
    uint8_t kf, kf_bars;               /* the phrase's kick twist at its end (KF_*), BARE's bars (1 or 2) */
    /* the echo: ping-pong */
    float e_lp[2];
    float e_route, e_smear, e_tape;    /* the echo's kind, by degrees: STRAIGHT's share, SMEAR's, TAPE's */
    float e_wow, e_flut;               /* TAPE's waver (turns) */
    float lim;                         /* the master limiter's gain */
#define NG 7
    float g[NG];                       /* the levels, gliding: kick, bass, perc, events, line, the events' send, the echo */
    float echo_d;                      /* the echo's delay in frames, gliding (a tempo moved, a time changed) */
    float dub_ph;                      /* DUB's waver (turns) */
    float frz_t;                       /* FREEZE: samples since it was pressed (0: not held) */
    uint8_t gap;                       /* GAP: this beat silent (the bar's last) */
    float gap_g;                       /* its gain, gliding (~2 ms) */
    float p_lp[2];                     /* the percussion's TONE's low-pass */
    float up[2];                       /* the music above the kick and the bass, the sample before (FREEZE's) */
    /* the sections */
    uint8_t sec, drops;
    uint8_t type[NPART];                 /* each part's type (PART_*: the bass's is its colour) */
    uint32_t origin;                   /* the bar the phrases count from: the last drop's */
    uint32_t last_drop;
    uint32_t builds;                   /* builds so far (their lengths' dice) */
    uint32_t sec_bar, sec_len;         /* bars into the section; a break's length (0: until cued), a build's */
    uint8_t bk;                        /* the build's kind (PS_BUILD_ROLL ..), taken as it begins */
    uint8_t be;                        /* its ending (PS_END_BREATH ..), taken so too */
    int8_t lift;                       /* STAIRS: semitones the bass, the line, the atmos and the events go up (0 none) */
    float hp_hz, hp_b[2], hp_u[2][2];  /* FILTER's high-pass: its cutoff (glides; 0 none), the bass's, the upper's */
    float lp_g, lp_u[2][2];            /* SINK's low-pass over the upper: its share (glides), its states */
    uint32_t cue_seen, drop_seen, build_seen;
    uint8_t fill_beat;                 /* this beat a fill (cued by a black key) */
    float part_g[NPART];               /* the parts' gains, gliding to their mutes */
    float x_t;                         /* the crash's age (s, < 0 silent) */
    float z_g, z_low, z_band;          /* the riser: its level (glides), its band-pass */
    /* the atmos */
    float a_lvl;                       /* its level now (glides between the sections) */
    float a_ph[3][2], a_mph[3];        /* the chord's notes, left and right; each note's modulator */
    float a_m[7];                      /* the atmos's type, moving to it: AM_* */
    float a_pg;                        /* PULSE's gate, following its pattern (~4 ms) */
    float a_genv, a_gtar;              /* GRAIN: the chord's gate now, the grain's level */
    uint32_t a_gn, a_rng;              /* samples to the next grain, its own dice (the music's stay as they were) */
    float a_vg[3];                     /* the chord's notes' gains (lighter: the upper two fade, and back) */
    float a_lfo, a_lp[2];              /* the slow wander; the chord's low-pass */
    float a_low, a_band;               /* the air's band-pass */
} s;
static volatile uint32_t cue_n, drop_n, build_n;   /* the UI's cues (a break, the drop, a build), counted: the render
                                                    * acts on a change */
static volatile uint8_t shed;          /* lighter: the render near its time (APP_OVERLOAD) */
static volatile int key_oct;           /* the keys' events: octaves up or down */
static volatile uint8_t line_new;      /* the line drawn anew at the next bar */
static volatile uint32_t kit_fwd, kit_back;   /* MAIN's ALGORITHM, its turns each way, counted: the render acts on
                                               * the change as a bar begins */

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
    float clap_f, z_f, air_f;
    float lp_f;                        /* SINK's low-pass */
    float k_drive;                     /* DRIVE: the kick's, of its type's */          /* the clap's, the riser's and the atmos's air's band-passes */
    float a_inc[3];                    /* the atmos's notes, turns a sample */
    float k_body;                      /* DECAY: the kick's body, of its type's (50: 1) */
    float k_cut, k_fade;               /* DECAY: under 50 the kick cut short after its drive (1/s, 0: none); over, its
                                        * end's fade held off (the power of t over its length, 1: straight) */
    float b_lp;                        /* the bass's low-pass (Dark) */
    float acid;                        /* ACID: how far through it (0..1; 0 none): the bass's bite opening, staying */
    float b_idx_k;                     /* DECAY: how fast the bass's bite closes (1/s) */
    float b_floor;                     /* DECAY under 50: what stays of the bite, of the world's (1: all) */
    float b_len;                       /* LENGTH: its notes', of the world's */
    float o_len;                       /* OPEN: the open hat's length, of its type's (50: 1) */
    float l_drive, l_cmp;              /* the line's DRIVE (0: none), its loudness kept */
    float a_rate;                      /* MOTION: the atmos's sway, turns a sample */
    float a_more;                      /* MOTION past the middle: the sway deeper and opening the atmos (0..1) */
    float p_thin, p_lp;                /* the percussion's TONE: over 50 the hats thinner (their high-passes' a, less);
                                        * under, a low-pass over all of it (its coefficient; 1: none) */
} cf;

#define ECHO_N 20480u                  /* (a dotted eighth at 125 bpm, the slowest a world can be moved to: 15876; DUB
                                        * stretches it as far as this holds, half again at 145) */
static PLAT_POOL int16_t echo_buf[2][ECHO_N];
static uint32_t echo_w;

/* commands from the UI: keys (the render drains them as a block begins) */
#define CMD_N 256u                     /* (a key's up must not be lost: room for a dense MIDI burst and the keys) */
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

/* a high-pass, 12 dB an octave (two one-poles): the sub kept to the kick and the bass */
typedef struct { float y1, x1, y2, x2; } hp2_t;
#define HP150 0.97885f                 /* exp(-2 pi 150 / 44100) */
static float hp2(hp2_t *h, float x, float a)
{
    float y1 = a * (h->y1 + x - h->x1), y2;
    h->x1 = x;
    h->y1 = y1;
    y2 = a * (h->y2 + y1 - h->x2);
    h->x2 = y1;
    h->y2 = y2;
    return y2;
}
static hp2_t echo_hp;                  /* the echo's input */
/* SMEAR: three all-passes a side in the echo's loop, each repeat through them again (lengths prime, the sides apart) */
#define AP_N 6
static const uint16_t AP_LEN[AP_N] = {142, 379, 607, 163, 401, 641};
static float ap_buf[142 + 379 + 607 + 163 + 401 + 641];
static uint16_t ap_pos[AP_N];
static float smear(int ch, float x)
{
    int k;
    float *b = ap_buf;
    for (k = 0; k < ch * 3; k++)
        b += AP_LEN[k];
    for (k = ch * 3; k < ch * 3 + 3; k++) {
        float y, d = b[ap_pos[k]];
        y = d - 0.55f * x;
        b[ap_pos[k]] = x + 0.55f * y;
        if (++ap_pos[k] >= AP_LEN[k])
            ap_pos[k] = 0;
        b += AP_LEN[k];
        x = y;
    }
    return x;
}

static int build_breath(uint32_t beat);
#include "events.c"
#include "fx.c"
#include "line.c"

/* the queue's commands: bits 16.. the kind (0 a key, 1 an effect, 2 a mute, 3 out, 4 a fill) */
enum { CMD_KEY, CMD_FX, CMD_MUTE, CMD_OUT, CMD_FILL, CMD_TYPE, CMD_REC, CMD_HALF, CMD_GAP };
static const char *const TYPE_NAME[6][PS_NTYPE] = {   /* PART_*; as many as each has */
    [PART_KICK] = {"SOLID", "HARD", "KNOCK", "ZIP", "CRACK"},
    [PART_BASS] = {"ROUND", "BITTEN", "METAL", "HOLLOW", "SNAP", "NASAL"},
    [PART_LINE] = {"ACID", "GLASS", "GROWL", "REED", "BEND", "CRUSH"},
    [PART_PERC] = {"HISS", "TIN", "TICK", "GRIT", "SEED"},
    [PART_EVENTS] = {"ALIVE", "METAL", "LIQUID", "SHARD", "GHOST", "RATCHET"},
    [PART_ATMOS] = {"MIST", "CHIME", "DRONE", "GRAIN", "PULSE"},
};
static uint32_t ntypes(int part)
{
    uint32_t n = 0;
    while (n < PS_NTYPE && TYPE_NAME[part][n])
        n++;
    return n;
}
/* the atmos's types (ALGORITHM on the ATMOS page), its values moving from one to the next:
 *   MIST   the world's own: soft, a fifth's shimmer
 *   CHIME  the modulator off the harmonics, brighter, the notes closer: bells far off
 *   DRONE  the modulator on the note, deeper, darker, the notes apart: a slow beating hum
 *   GRAIN  the chord broken into grains, more air: a forest's crackle
 *   PULSE  the chord cut in time with the music, sixteenths in a pattern of the bar, brighter, less air: a pad that
 *          breathes with the groove */
enum { AM_RATIO, AM_IDX, AM_DET, AM_LP, AM_AIR, AM_GRAIN, AM_PULSE, AM_N };
static const float ATMOS_TYPE[PS_NTYPE][AM_N] = {
    /* ratio  index  detune   lowpass  air    grain   (levels matched to MIST's: the index and the lowpass) */
    {2.0f, 1.0f, 0.0016f, 1.0f, 1.0f, 0.0f},
    {3.5f, 0.7f, 0.0008f, 1.6f, 0.6f, 0.0f},
    {1.0f, 1.8f, 0.006f, 0.5f, 0.5f, 0.0f},
    {2.0f, 1.2f, 0.0016f, 1.2f, 1.6f, 1.0f},
    {2.0f, 1.3f, 0.003f, 1.5f, 0.4f, 0.0f, 1.0f},
};

/* the percussion's types (ALGORITHM on the PERC page): the hats' source (moving to it) and each one's decay (taken as
 * it strikes), of the world's:
 *   HISS  the world's own: noise
 *   TIN   six squares, inharmonic, a little noise, ringing longer: a small cheap cymbal
 *   TICK  short and thin, the clap's tail clipped: dry ticks
 *   GRIT  the noise held in steps (a lower rate), the clap too: dust in the machine
 *   SEED  the hats a narrow band of noise round 4.5 kHz (not all of its top), swelling in over ~15 ms, longer, crackling in
 *         sparse grains: a shaker's seeds */
enum { PT_HAT, PT_OPEN, PT_CLAP, PT_RIDE, PT_THIN, PT_NOISE, PT_METAL, PT_GRIT, PT_LEVEL, PT_ATTACK, PT_N };
static const float PERC_TYPE[PS_NTYPE][PT_N] = {
    /* decays: hat  open   clap   ride   thin   noise  metal  grit   level (matched to HISS's) */
    {1.0f, 1.0f, 1.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.0f},
    {1.4f, 1.3f, 1.0f, 1.3f, 0.0f, 0.3f, 1.0f, 0.0f, 1.15f},
    {0.45f, 0.5f, 0.4f, 0.5f, 0.06f, 1.0f, 0.0f, 0.0f, 1.25f},
    {1.1f, 1.1f, 1.2f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.55f},
    {2.2f, 0.8f, 0.9f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f, 1.13f, 0.015f},   /* (attack: s) */
};

/* the kick's types (ALGORITHM on the KICK page), taken as it strikes. None longer than the world's: after it is the
 * bass's place.
 *   SOLID  the world's own
 *   HARD   the pitch falling faster, the click louder, driven: a harder edge
 *   KNOCK  a modulator a little off the note, gone in a few ms, the body shorter: wood under the boom
 *   ZIP    the pitch falling from higher and slower, the body shorter: the zip of a dark kick
 *   CRACK  a burst of bright noise on its first ms, driven with it, the fall fast, the body tight: the snap of a
 *          hi-tech kick */
enum { KT_FALL, KT_SWEEP, KT_BODY, KT_CLICK, KT_DRIVE, KT_FM, KT_LEVEL, KT_NOISE, KT_N };
static const float KICK_TYPE[PS_NTYPE][KT_N] = {
    /* fall (s)  sweep  body   click  drive  fm     level (matched to SOLID's) */
    {0.011f, 1.0f, 0.45f, 1.0f, 1.8f, 0.0f, 1.0f},
    {0.008f, 1.2f, 0.40f, 1.6f, 3.2f, 0.0f, 0.77f},
    {0.011f, 0.9f, 0.35f, 0.6f, 1.8f, 2.5f, 1.08f},
    {0.022f, 1.6f, 0.40f, 0.8f, 2.0f, 0.0f, 1.0f},
    {0.007f, 1.1f, 0.36f, 0.4f, 2.4f, 0.0f, 0.92f, 1.0f},
};

int ps_ntypes(int part) { return part >= 0 && part < NPART ? (int)ntypes(part) : 1; }
const char *ps_type_name(int part, int t)
{
    return part >= 0 && part < NPART && t >= 0 && t < PS_NTYPE && TYPE_NAME[part][t] ? TYPE_NAME[part][t] : "WORLD";
}
void ps_type(int part, int t)
{
    if (part >= 0 && part < NPART && t >= 0 && t < ps_ntypes(part))
        cmd_post((uint32_t)CMD_TYPE << 16 | (uint32_t)part << 8 | (uint32_t)t);
}
int ps_type_now(int part) { return part >= 0 && part < NPART ? s.type[part] : 0; }
void ps_colour(int c) { ps_type(PART_BASS, c); }
int ps_colour_now(void) { return s.type[PART_BASS]; }
void ps_line_new(void) { line_new = 1; }
static volatile int shape_want;        /* PS_SHAPE_* (AUTO: the world's) */

/* the kit (ALGORITHM on MAIN): the turns of a bar taken together as the next bar begins (stopped: at once). Forward,
 * another type for one part or a few (one turn: one, now and then two; more turns: as many, up to three), chosen
 * among the parts heard now (not muted or out, the kick and the bass not in a break, the line not resting, the events
 * not before they come in), not the ones the last kit changed if others are there, each to a type other than its
 * own; each part takes it at its next note. The groove too, the bass heard in the groove: another shape for four
 * bars over GROOVE's (AUTO's or the one held, which stays; GROOVE's ALGORITHM turned meanwhile: gone). Back, the kit
 * before (up to eight; the kit's shape gone). */
#define KIT_GROOVE PS_KIT_GROOVE       /* the groove's bit in the kit's parts (after the parts') */
#define KIT_HIST 8
static struct {
    uint32_t fwd_seen, back_seen;
    uint8_t hist[KIT_HIST][NPART], depth, top;
    uint8_t last;                      /* the parts the last kit changed */
} kit;
void ps_kit(int turns)
{
    if (turns > 0)
        kit_fwd += (uint32_t)turns;
    else
        kit_back += (uint32_t)-turns;
}

static uint32_t kit_heard(void)        /* the parts heard now, 1 << PART_* (and the groove's, where AUTO's) */
{
    uint32_t m = (1u << (NPART + 1)) - 1u, bar = s.tick / TPBAR;
    m &= ~(uint32_t)(fx.muted | fx.out);
    if (s.sec == SEC_BREAK)
        m &= ~(1u << PART_KICK | 1u << PART_BASS);
    if (!ln.playing)
        m &= ~(1u << PART_LINE);
    if ((bar < 4u && !s.drops) || s.sec == SEC_BUILD)
        m &= ~(1u << PART_EVENTS);
    if (s.sec != SEC_MAIN || !(m >> PART_BASS & 1u))
        m &= ~(1u << KIT_GROOVE);
    return m;
}

static void kit_forward(int n)         /* n parts or so, or the groove, another type: the kit before kept (forward) */
{
    uint32_t heard, pick = 0, p;
    heard = kit_heard();
    if (!heard)
        heard = (1u << NPART) - 1u;
    if (s.kit_bars)                    /* (a shape still over AUTO's: not again) */
        heard &= ~(1u << KIT_GROOVE);
    for (; n > 0; n--) {               /* one at a time: from the heard not changed last, else the heard */
        uint32_t from = heard & ~pick & ~(uint32_t)kit.last, c = 0, k;
        if (!from)
            from = heard & ~pick;
        if (!from)
            break;
        for (p = 0; p <= KIT_GROOVE; p++)
            c += from >> p & 1u;
        k = rnd() % c;
        for (p = 0; p <= KIT_GROOVE; p++)
            if ((from >> p & 1u) && k-- == 0u)
                break;
        pick |= 1u << p;
    }
    for (p = 0; p < NPART; p++)        /* the kit as it was, kept for going back */
        kit.hist[kit.top][p] = s.type[p];
    kit.top = (uint8_t)((kit.top + 1u) % KIT_HIST);
    if (kit.depth < KIT_HIST)
        kit.depth++;
    for (p = 0; p < NPART; p++)
        if (pick >> p & 1u)
            s.type[p] = (uint8_t)((s.type[p] + 1u + rnd() % (ntypes((int)p) - 1u)) % ntypes((int)p));
    if (pick >> KIT_GROOVE & 1u) {     /* the groove: another shape than this bar's, four bars */
        static const uint8_t SHAPE[3] = {0, 1, 3};
        uint32_t now = (s.tick / TPBAR - s.origin) % 8u == s.trip_at ? 2u : s.pat, j = rnd() % 3u;
        if (SHAPE[j] == now)
            j = (j + 1u + rnd() % 2u) % 3u;
        s.kit_pat = SHAPE[j];
        s.kit_bars = 4;
    }
    kit.last = (uint8_t)pick;
    if (pick) {
        ps_ui.kit_parts = (uint8_t)pick;
        ps_ui.kits++;
    }
    ps_ui.kit_depth = kit.depth;
}

/* MORPH: now and then, on its own, the kit forward (a drop's; a 32-bar block's in a long groove, half as likely): more
 * likely and more parts the higher, and the longer since the last the likelier (half again each chance gone by) */
static uint8_t morph_wait;             /* the chances gone by since the kit last changed on its own */
static void kit_morph(float chance)
{
    float m = (float)par[P_MORPH] / 100.0f;
    if (m <= 0.0f)
        return;
    if ((float)(rnd() % 1000u) >= 1000.0f * m * chance * (1.0f + 0.5f * (float)morph_wait)) {
        if (morph_wait < 255u)
            morph_wait++;
        return;
    }
    morph_wait = 0;
    kit_forward(1 + ((float)(rnd() % 100u) < 60.0f * m) + ((float)(rnd() % 100u) < 25.0f * m));
}

static void kit_step(void)
{
    int net = (int)(kit_fwd - kit.fwd_seen) - (int)(kit_back - kit.back_seen);
    uint32_t pick = 0, p;
    int n;
    kit.fwd_seen = kit_fwd;
    kit.back_seen = kit_back;
    if (s.kit_bars)                    /* a bar of the groove's shape gone */
        s.kit_bars--;
    if (shape_want != s.shape_was) {   /* GROOVE's ALGORITHM turned: the kit's shape gone (before this kit's) */
        s.shape_was = (uint8_t)shape_want;
        s.kit_bars = 0;
    }
    if (net < 0) {                     /* back: the kits before, as far as they go */
        uint8_t was[NPART];
        for (p = 0; p < NPART; p++)
            was[p] = s.type[p];
        if (s.kit_bars && kit.depth) {
            s.kit_bars = 0;
            pick |= 1u << KIT_GROOVE;
        }
        for (; net < 0 && kit.depth; net++) {
            kit.top = (uint8_t)((kit.top + KIT_HIST - 1u) % KIT_HIST);
            kit.depth--;
            for (p = 0; p < NPART; p++)
                s.type[p] = kit.hist[kit.top][p];
        }
        for (p = 0; p < NPART; p++)
            if (s.type[p] != was[p])
                pick |= 1u << p;
        kit.last = (uint8_t)pick;
    } else if (net > 0) {
        n = net > 1 ? (net > 3 ? 3 : net) : rnd() % 100u < 30u ? 2 : 1;
        kit_forward(n);
    }
    if (pick) {
        ps_ui.kit_parts = (uint8_t)pick;
        ps_ui.kits++;
    }
    ps_ui.kit_depth = kit.depth;
    ps_ui.kit_shape = (int8_t)(s.kit_bars ? (s.kit_pat == 1u ? PS_SHAPE_GALLOP : s.kit_pat == 3u ? PS_SHAPE_OFF
                                                                                : PS_SHAPE_ROLL) : PS_SHAPE_AUTO);
}
static volatile int build_want;        /* PS_BUILD_* (AUTO: the world leans) */
static volatile int end_want;          /* PS_END_* (AUTO: any but the last) */
static volatile int echo_kind;         /* PS_ECHO_* */
void ps_bass_shape(int k) { shape_want = k >= 0 && k < PS_NSHAPE ? k : PS_SHAPE_AUTO; }
int ps_bass_shape_now(void) { return shape_want; }
const char *ps_bass_shape_name(int k)
{
    static const char *const N[PS_NSHAPE] = {"AUTO", "K-B-B-B", "GALLOP", "OFFBEAT"};
    return N[(unsigned)k % PS_NSHAPE];
}
void ps_echo_kind(int k) { echo_kind = k >= 0 && k < PS_NECHO ? k : PS_ECHO_PING; }
int ps_echo_kind_now(void) { return echo_kind; }
const char *ps_echo_kind_name(int k)
{
    static const char *const N[PS_NECHO] = {"PING-PONG", "STRAIGHT", "TAPE", "SMEAR"};
    return N[(unsigned)k % PS_NECHO];
}
void ps_build_kind(int k) { build_want = k >= 0 && k < PS_NBUILD ? k : PS_BUILD_AUTO; }
int ps_build_kind_now(void) { return build_want; }
void ps_build_end(int k) { end_want = k >= 0 && k < PS_NEND ? k : PS_END_AUTO; }
int ps_build_end_now(void) { return end_want; }
const char *ps_end_name(int k)
{
    static const char *const N[PS_NEND] = {"AUTO", "BREATH", "RUSH", "VOID", "TAPE", "REVERSE", "ECHO", "PICKUP"};
    return N[(unsigned)k % PS_NEND];
}
const char *ps_build_name(int k)
{
    static const char *const N[PS_NBUILD] = {"AUTO", "ROLL", "KICK", "FILTER", "TRIPLET", "STUTTER", "POLY", "SINK",
                                             "FAKE", "HALF", "ACID", "STAIRS", "GATE"};
    return N[(unsigned)k % PS_NBUILD];
}
void ps_octave(int o) { key_oct = o < -1 ? -1 : o > 2 ? 2 : o; }
void ps_shed(int on) { shed = (uint8_t)(on != 0); }
int ps_shedding(void) { return shed; }
void ps_fx(int id, int on) { cmd_post((uint32_t)CMD_FX << 16 | (uint32_t)(id & 31) | (uint32_t)(on != 0) << 5); }
const char *ps_fx_name(int id) { return FX_NAME[(unsigned)id % FX_N]; }
void ps_mute(int part) { cmd_post((uint32_t)CMD_MUTE << 16 | (uint32_t)(part & 7)); }
void ps_out(int part, int on) { cmd_post((uint32_t)CMD_OUT << 16 | (uint32_t)(part & 7) | (uint32_t)(on != 0) << 5); }
void ps_fill(void) { cmd_post((uint32_t)CMD_FILL << 16); }
void ps_half(int on) { cmd_post((uint32_t)CMD_HALF << 16 | (uint32_t)(on != 0) << 5); }
void ps_gap(void) { cmd_post((uint32_t)CMD_GAP << 16); }
void ps_rec(int on) { cmd_post((uint32_t)CMD_REC << 16 | (uint32_t)(on != 0)); }
int ps_take(void) { return take.n; }
int ps_rec_state(void) { return take.rec ? 2 : take.arm ? 1 : 0; }
int ps_rec_bar(int *of)
{
    uint32_t st = take.start, en = take.end, t = s.tick;
    if (of)
        *of = (int)((en - st) / TPBAR);
    return t < st ? 1 : (int)((t - st) / TPBAR) + 1;
}

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
    case CMD_HALF:
        fx.half = (uint8_t)b;
        break;
    case CMD_GAP:
        fx.gap_req = 1;
        break;
    case CMD_TYPE:
        if ((c >> 8 & 7u) < NPART && (c & 7u) < ntypes((int)(c >> 8 & 7u)))
            s.type[c >> 8 & 7u] = (uint8_t)(c & 7u);
        break;
    case CMD_REC:                      /* 1 (a tap): armed, from the next bar (armed: not; taking: it ends with the
                                        * bar, the loop so long); 0 (held): the loop gone */
        if (!(c & 1u))
            take.n = take.arm = take.rec = 0;
        else if (take.rec)
            take_close();
        else
            take.arm = (uint8_t)!take.arm;
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
static int filter_in(int part, uint32_t beat);
static uint32_t build_len(void);
static uint32_t roll_every(uint32_t pb);
static int build_fake(void);
static int build_grooves(void);
static uint32_t poly_grid(uint32_t pb, uint32_t *st);
static int half_build(void);
static uint32_t bass_pat(uint32_t rb)  /* the shape of bar rb (the kit's over AUTO's while it lasts) */
{
    return s.kit_bars ? s.kit_pat : rb % 8u == s.trip_at ? 2u : s.pat;
}

/* the kick: four to the bar, and now and then, as a phrase ends (Fills: how often), a twist: DOUBLE (a kick on the
 * bar's last sixteenth too, into the next bar's: K . . K | K), ROLL (the last beat's sixteenths all the kick's, the
 * bass resting), BARE (its last bar or two without the kick, the bass alone), HALF (its last bar the kick on 1 and 3
 * only). Every 32nd bar, with Fills, its last beat without (as it was). */
enum { KF_NONE, KF_DOUBLE, KF_ROLL, KF_BARE, KF_HALF, KF_N };
static uint32_t kick_fill(uint32_t rb)  /* the twist acting in bar rb (KF_NONE: none) */
{
    if (s.sec != SEC_MAIN || s.kf == KF_NONE)
        return KF_NONE;
    if (s.kf == KF_BARE)
        return rb % 8u >= 8u - s.kf_bars ? KF_BARE : KF_NONE;
    return rb % 8u == 7u ? s.kf : KF_NONE;
}
static int kick_at(uint32_t rb, uint32_t beat, uint32_t t)   /* the kick strikes at tick t of beat */
{
    uint32_t kf = kick_fill(rb);
    if (beat == 3u && ((kf == KF_DOUBLE && t == 18u) || (kf == KF_ROLL && t % 6u == 0u)))
        return 1;
    if (t)
        return 0;
    return !(par[P_FILLS] && rb % 32u == 31u && beat == 3u) && kf != KF_BARE && !(kf == KF_HALF && (beat & 1u));
}

/* a build's roll in a lane: n steps (16, or TRIPLET's 24), its every (roll_every); louder as it goes; the breath
 * empty */
static int lane_roll(uint8_t out[24], uint32_t lo)
{
    uint32_t len = build_len(), last = s.sec_bar + 1u >= len, trip = s.bk == PS_BUILD_TRIP, per = trip ? 6u : 4u, i;
    for (i = 0; i < per * 4u; i++) {
        uint32_t pb = s.sec_bar * 4u + i / per;
        if ((i % per) * (TPB / per) % roll_every(pb) == 0u && !build_breath(i / per))
            out[i] = (uint8_t)(lo + (pb >= 2u * len) + last);
    }
    return (int)(per * 4u);
}

static void lane_poly(uint8_t out[24])   /* POLY's kick: each sixteenth a dotted grid's stroke falls in */
{
    uint32_t len = build_len(), i, d;
    for (i = 0; i < 16u; i++) {
        uint32_t pb = s.sec_bar * 4u + i / 4u, st, grid = poly_grid(pb, &st);
        if (build_breath(i / 4u))
            continue;
        for (d = 0; d < 6u; d++)
            if (((pb - st) * TPB + (i % 4u) * 6u + d) % grid == 0u)
                out[i] = 3;
    }
}

/* the bar now as it will play (psy.h): read by the UI while the render moves on (a frame's glance, no lock) */
int ps_lane(int lane, uint8_t out[24])
{
    const world_t *w = &W[world];
    uint32_t tick = s.tick ? s.tick - 1u : 0u, bar = tick / TPBAR, rb = bar - s.origin, i;
    int groove = s.sec == SEC_MAIN, n = 16, build = s.sec == SEC_BUILD;
    int gro = build_grooves(), fake = build_fake();   /* (the groove goes on; FAKE's false drop's bar) */
    for (i = 0; i < 24u; i++)
        out[i] = 0;
    if (!playing)
        return 16;
    switch (lane) {
    case LANE_KICK:
        if (build && s.bk == PS_BUILD_KICK) {
            n = lane_roll(out, 1u);
            break;
        }
        if (build && s.bk == PS_BUILD_POLY) {
            lane_poly(out);
            break;
        }
        if (groove || gro)
            for (i = 0; i < 16u; i++)
                if ((fx.half || half_build() ? i % 8u == 0u : kick_at(rb, i / 4u, (i % 4u) * 6u)) && !build_breath(i / 4u))
                    out[i] = 3;   /* (HALF: on 1 and 3) */
        if (fake)
            out[0] = 3;
        break;
    case LANE_BASS:
        if (build && s.bk == PS_BUILD_ACID) {   /* ACID: every sixteenth, every third an octave up */
            for (i = 0; i < 16u; i++)
                out[i] = build_breath(i / 4u) ? 0 : (s.sec_bar * 16u + i) % 3u == 2u ? 3 : 2;
            break;
        }
        if (!groove && !gro && !fake)
            break;
        if (fx.half || half_build()) {                 /* HALF: an eighth's roll after the kick on 1 and 3 */
            for (i = 0; i < 16u; i++)
                out[i] = i % 8u && !(i & 1u) && !build_breath(i / 4u) ? 2 : 0;
            break;
        }
        if (bass_pat(rb) == 2u) {      /* triplets: six a beat, the kick on the first */
            n = 24;
            for (i = 0; i < 24u; i++)
                out[i] = i % 6u ? 2 : 0;
        } else
            for (i = 0; i < 16u; i++)
                out[i] = i % 4u == 0u || (bass_pat(rb) == 1u && i % 4u == 1u) || (bass_pat(rb) == 3u && i % 4u != 2u)
                       ? 0 : 2;
        for (i = 0; i < (uint32_t)n; i++) {
            uint32_t b = i * 4u / (uint32_t)n;
            if ((fill_now(rb, 3) && b == 3u) || build_breath(b) || !filter_in(PART_BASS, b) || (fake && b))
                out[i] = 0;           /* (a fill's beat, the breath, not in yet; FAKE's: its first beat only) */
        }
        if (kick_fill(rb) == KF_ROLL || kick_fill(rb) == KF_DOUBLE)   /* the last beat: the kick's (DOUBLE: K-B-B-K) */
            for (i = (uint32_t)n * 3u / 4u; i < (uint32_t)n; i++)
                out[i] = kick_fill(rb) == KF_DOUBLE && !fill_now(rb, 3) && (n == 16 ? i == 13u || i == 14u
                                                                                    : i == 19u || i == 21u) ? 2 : 0;
        break;
    case LANE_PERC:
        if (fake) {                    /* the false drop: a beat, then the clap's sixteenths */
            for (i = 4; i < 16u; i++)
                out[i] = (uint8_t)(1u + (i >= 8u) + (i >= 12u));
        } else if (build && s.bk == PS_BUILD_POLY) {
            out[4] = out[12] = 3;
            if (build_breath(3))
                out[12] = 0;
        } else if (build && s.bk == PS_BUILD_ACID) {   /* ACID: the hats on the offbeats */
            for (i = 2; i < 16u; i += 4u)
                out[i] = build_breath(i / 4u) ? 0 : 2;
        } else if (build && !gro) {    /* the roll (KICK's in the kick's lane) */
            if (s.bk != PS_BUILD_KICK)
                n = lane_roll(out, 1u);
        } else if (groove || gro) {
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
            if (groove && fill_now(rb, 3))
                for (i = 12; i < 16u; i++)
                    out[i] = (uint8_t)(1u + (i - 12u) / 2u + (i == 15u));
            for (i = 0; i < 16u; i++)  /* (the breath; FILTER's not in yet) */
                if (build_breath(i / 4u) || !filter_in(PART_PERC, i / 4u))
                    out[i] = 0;
        }
        break;
    case LANE_LINE:
        if (ln.playing)
            for (i = 0; i < 16u; i++) {
                uint32_t k = (rb % 2u) * 16u + i;
                if (ln_on(w, k))
                    out[i] = (uint8_t)(1 + clampi(ln_note(w, k) + 2, 0, 30));
            }
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
int ps_line_range(void) { return ln_range(&W[world]) + 8; }   /* (+2 below the register, Dark's +6 above) */

void ps_init(void)
{
    int k;
    want_world = world = 2;            /* COLD SIGNAL */
    morph_wait = 0;
    want_play = playing = 1;
    for (k = 0; k < P_NPARAMS; k++)
        par[k] = PAR[k].def;
    s.k_t = s.b_t = -1.0f;
    s.h_t = s.o_t = s.c_t = s.r_t = -1.0f;
    s.h_k = s.o_k = s.c_k = s.r_k = 1.0f;
    s.h_a = s.o_a = 0.0001f;
    s.k_ty = KICK_TYPE[0];
    s.p_w[0] = s.p_w[3] = 1.0f;
    s.gap_g = 1.0f;
    s.rng = 0x9E3779B9u;
    s.lim = 1.0f;
    s.a_vg[0] = s.a_vg[1] = s.a_vg[2] = 1.0f;
    for (k = 0; k < AM_N; k++)
        s.a_m[k] = ATMOS_TYPE[0][k];
    s.a_rng = 0x2545F491u;
    for (k = 0; k < EV_NVOICE; k++)
        ev.v[k].t = -1.0f;
    ln.t = -1.0f;
    ev.last_type = -1;
    ev.rk = -1;
    ev.lastk = -1;
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
    if (how == PS_CUE_DROP)
        drop_n++;
    else if (how == PS_CUE_BUILD)
        build_n++;
    else
        cue_n++;
}
int ps_section(void) { return s.sec; }
int ps_stat(int which)
{
    const world_t *w = &W[world];
    int r = 0;
    uint32_t i;
    if (which == 7)
        return fx.out;
    if (which == 8)
        return s.gap | fx.gap_req << 1 | fx.half << 2;
    if (which <= 3)
        return which == 0 ? (int)ev.fired : which == 1 ? ev.nmem : which == 2 ? ev.last_type : (int)ln.drawn;
    for (i = 0; i < LN_STEPS; i++)     /* the line's steps as heard now: 4 on, 5 the highest note, 6 sliding */
        r = which == 4 ? r + ln_on(w, i) : which == 5 ? (i && r > ln_note(w, i) ? r : ln_note(w, i))
                                                     : r + ln_slide(w, i);
    return r;
}
const char *ps_event_name(int type)
{
    return type >= EV_NTYPE ? KEY_SOUND[(unsigned)(type - EV_NTYPE) % 16u].name : EV_NAME[(unsigned)type % EV_NTYPE];
}
int ps_rhythm(void) { return ev.rk; }
const char *ps_rhythm_name(int k)
{
    static const char *const N[11] = {"3/16", "1/8", "1/8T", "GALLOP", "SCATTER", "WANDER", "LEAP", "FASTER", "SLOWER",
                                      "CLIMB", "FALL"};
    return k >= 0 && k < 27 && key_black(k) ? N[key_blacki(k)] : "";
}

static void mods(const world_t *w)     /* the macros and the moved tempo and root, as a block begins */
{
    m.energy = (float)(par[P_ENERGY] - 50) / 50.0f;
    m.detail = (float)(par[P_DETAIL] - 50) / 50.0f;
    m.dark = (float)(par[P_DARK] - 50) / 50.0f;
    m.drift = (float)par[P_DRIFT] / 100.0f;
    m.root = w->root + par[P_ROOT];
    m.bpm = ext_bpm > 0.0f ? ext_bpm : w->bpm + (float)par[P_TEMPO];
}

/* a build's bars but FAKE's last (its false drop's) */
static uint32_t build_len(void) { return s.bk == PS_BUILD_FAKE && s.sec_len > 1u ? s.sec_len - 1u : s.sec_len; }
/* its ending's beats (its last bar's last: VOID the bar, or two in a 2-bar build; TAPE, ECHO two; the rest one), at
 * beat here (FAKE: before its false drop) */
static int build_end(uint32_t beat)
{
    uint32_t n = s.be == PS_END_VOID ? (build_len() > 2u ? 4u : 2u) : s.be == PS_END_TAPE || s.be == PS_END_ECHO ? 2u : 1u;
    return s.sec == SEC_BUILD && s.sec_bar + 1u == build_len() && beat + n >= 4u;
}
static int build_breath(uint32_t beat)   /* silent here: its ending's (BREATH, PICKUP its last beat; VOID its beats;
                                          * ECHO its last; RUSH, TAPE, REVERSE none: the music goes on beneath) */
{
    return build_end(beat) && s.be != PS_END_RUSH && s.be != PS_END_TAPE && s.be != PS_END_REVERSE &&
           (s.be != PS_END_ECHO || beat == 3u);
}
static int build_fake(void) { return s.sec == SEC_BUILD && s.bk == PS_BUILD_FAKE && s.sec_bar >= build_len(); }
static int build_grooves(void)         /* the groove goes on through it (FILTER: by turns; STUTTER: repeated; HALF:
                                        * in half time; STAIRS: rising; GATE: cut) */
{
    return s.sec == SEC_BUILD && (s.bk == PS_BUILD_FILTER || s.bk == PS_BUILD_STUTTER || s.bk == PS_BUILD_HALF ||
                                  s.bk == PS_BUILD_STAIRS || s.bk == PS_BUILD_GATE);
}
static int half_build(void) { return s.sec == SEC_BUILD && s.bk == PS_BUILD_HALF; }
static int acid_build(void) { return s.sec == SEC_BUILD && s.bk == PS_BUILD_ACID; }
/* FILTER's parts come in by turns, not all at once: the kick from its start, the perc from its second quarter, the
 * bass (thinned by then) from its half */
static int filter_in(int part, uint32_t beat)
{
    uint32_t pb = s.sec_bar * 4u + beat;
    return s.bk != PS_BUILD_FILTER || part == PART_KICK || pb >= s.sec_len * (part == PART_PERC ? 1u : 2u);
}

/* a roll's step in ticks at beat pb of the build: quarters for its first quarter, eighths for its second, then
 * sixteenths (TRIPLET: eighth and sixteenth triplets; SINK the other way, slowing) */
static uint32_t roll_every(uint32_t pb)
{
    uint32_t len = build_len(), q = pb < len ? 0u : pb < 2u * len ? 1u : 2u;
    static const uint8_t E[3][3] = {{24, 12, 6}, {24, 8, 4}, {6, 12, 24}};
    uint32_t e = E[s.bk == PS_BUILD_TRIP ? 1 : s.bk == PS_BUILD_SINK ? 2 : 0][q];
    return s.be == PS_END_RUSH && pb + 1u == 4u * len ? e / 2u : e;   /* (RUSH: its last beat twice as fast) */
}
/* POLY's kick: dotted eighths against the bar (from its half dotted sixteenths, its last quarter sixteenths; RUSH's
 * last beat 32nds); *st the beat its grid counts from */
static uint32_t poly_grid(uint32_t pb, uint32_t *st)
{
    uint32_t len = build_len();
    *st = pb < 2u * len ? 0u : pb < 3u * len ? 2u * len : 3u * len;
    return s.be == PS_END_RUSH && pb + 1u == 4u * len ? 3u : *st == 0u ? 18u : *st == 2u * len ? 9u : 6u;
}

/* the build's roll, louder beat by beat: a clap's, or (KICK) the kick's; its last beat silent. POLY: the kick on
 * dotted eighths against the bar (from its half on dotted sixteenths, its last quarter sixteenths), the clap on 2
 * and 4. FAKE's last bar: the drop for a beat (the kick, the bass, the crash), then sixteenths of the clap, rising.
 * FILTER and STUTTER: none (the groove goes on: perc_step) */
static void build_step(uint32_t t, uint32_t beat)
{
    uint32_t len = build_len(), pb = s.sec_bar * 4u + beat;
    if (build_breath(beat))
        return;
    if (s.bk == PS_BUILD_ACID) {       /* ACID: the hats on the offbeats, harder as it goes (the bass: step) */
        if (t == 12u) {
            s.h_t = 0.0f;
            s.h_v = (0.5f + 0.5f * (float)pb / (float)(4u * len)) * (float)par[P_HATS] / 50.0f;
            ps_ui.hits[HIT_PERC]++;
        }
        return;
    }
    if (build_fake()) {
        if (beat == 0u) {
            if (t == 0u) {
                s.x_t = 0.0f;          /* the crash, as if */
                ps_ui.hits[HIT_PERC]++;
            }
        } else if (t % 6u == 0u) {
            s.c_t = 0.0f;
            s.c_v = 0.45f + 0.55f * (float)((beat - 1u) * 4u + t / 6u) / 11.0f;
            ps_ui.hits[HIT_PERC]++;
        }
        return;
    }
    if (s.bk == PS_BUILD_POLY) {
        uint32_t st, grid = poly_grid(pb, &st);
        if (((pb - st) * TPB + t) % grid == 0u) {
            s.k_t = 0.0f;
            s.k_ph = 0.0f;
            ps_ui.hits[HIT_KICK]++;
        }
        if (t == 0u && (beat == 1u || beat == 3u)) {
            s.c_t = 0.0f;
            s.c_v = 0.6f + 0.4f * (float)pb / (float)(4u * len);
            ps_ui.hits[HIT_PERC]++;
        }
        return;
    }
    if (t % roll_every(pb))
        return;
    if (s.bk == PS_BUILD_KICK) {
        s.k_t = 0.0f;
        s.k_ph = 0.0f;
        ps_ui.hits[HIT_KICK]++;
        return;
    }
    s.c_t = 0.0f;
    s.c_v = 0.35f + 0.65f * (float)pb / (float)(4u * len - 1u);
    ps_ui.hits[HIT_PERC]++;
}

/* bar: since PLAY (the parts come in over the first bars); rb: since the last drop (the phrases) */
static void perc_step(const world_t *w, uint32_t t, uint32_t beat, uint32_t bar, uint32_t rb, int fill)
{
    uint32_t six = t / 6u;             /* the sixteenth in the beat */
    static const float HAT_V[4] = {0.55f, 0.80f, 0.0f, 1.0f};
    float hats = (float)par[P_HATS] / 50.0f;
    if (s.sec == SEC_BUILD && !build_grooves()) {
        build_step(t, beat);
        return;
    }
    if (s.sec == SEC_BUILD) {          /* FILTER, STUTTER: the groove's (FILTER: from its second quarter, the hats harder
                                        * as it rises); its last beat silent */
        if (build_breath(beat) || !filter_in(PART_PERC, beat))
            return;
        if (s.bk == PS_BUILD_FILTER)
            hats *= 1.0f + 0.6f * ((float)s.sec_bar * 4.0f + (float)beat) / (float)(4u * s.sec_len);
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
    if (six == 0u && (half_build() ? beat == 2u : beat == 1u || beat == 3u) && bar >= 16u && s.out != 3u) {
        s.c_t = 0.0f;
        s.c_v = (float)par[P_CLAP] / 50.0f;
        ps_ui.hits[HIT_PERC]++;
    }
    if (w->ride && six % 2u == 0u && bar >= 24u && s.out != 4u) {
        s.r_t = 0.0f;
        s.r_v = six == 2u ? 1.0f : 0.6f;
    }
}

/* The lengths' sway: FILLS, BUILD and GAP give the usual length, DRIFT how often it is the next one down or up
 * instead. Dice of their own, a hash of where and which (the music's own dice stay as they were): at DRIFT 0 the
 * lengths are the knobs' exactly. */
static float vary_u(uint32_t at, uint32_t salt)   /* 0..1, the same for the same place */
{
    uint32_t x = at * 0x9E3779B9u ^ salt ^ s.drops * 0x85EBCA6Bu;
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return (float)(x >> 8) / 16777216.0f;
}

static int vary_step(uint32_t at, uint32_t salt, float p)   /* -1, 0 or +1: down or up with p/2 each */
{
    float u = vary_u(at, salt);
    return u < 0.5f * p ? -1 : u < p ? 1 : 0;
}

#define VARY_P (0.6f * m.drift)        /* (DRIFT 50: three lengths in ten another) */

static int fill_now(uint32_t bar, uint32_t beat)   /* this beat a fill's (the bass rests) */
{                                      /* FILLS' period, each 32 bars: its own, or half or twice it */
    static const uint8_t EVERY[6] = {0, 32, 16, 8, 4, 4};
    int k = clampi(par[P_FILLS], 0, 4);
    uint32_t e;
    if (!k || beat != 3u)
        return 0;
    k += vary_step(bar / 32u, 0xF111u, VARY_P);   /* (a longer period: a step down the list) */
    e = k <= 0 ? 64u : EVERY[k];
    return bar % e == e - 1u;
}

static void enter(int sec, uint32_t len)
{
    s.sec = (uint8_t)sec;
    s.sec_bar = 0;
    s.sec_len = len;
    s.kit_bars = 0;                    /* (the kit's shape: the groove's only) */
}

static void enter_build(const world_t *w)   /* a build: BUILD's bars, its kind (AUTO: by the world's leaning, not the
                                             * last one again) */
{
    int k = build_want;
    if (k == PS_BUILD_AUTO) {
        uint32_t sum = 0, r, i;
        for (i = 0; i < PS_NBUILD - 1u; i++)
            sum += s.bk == PS_BUILD_ROLL + (int)i ? 0u : w->bk[i];
        r = sum ? rnd() % sum : 0u;
        for (i = 0; i < PS_NBUILD - 1u; i++) {
            uint32_t p = s.bk == PS_BUILD_ROLL + (int)i ? 0u : w->bk[i];
            if (r < p)
                break;
            r -= p;
        }
        k = PS_BUILD_ROLL + (int)(i < PS_NBUILD - 1u ? i : 0u);
    }
    s.bk = (uint8_t)k;
    ps_ui.build_kind = (int8_t)k;
    k = end_want;                      /* its ending (AUTO: any but the last) */
    if (k == PS_END_AUTO) {
        k = PS_END_BREATH + (int)(rnd() % (s.be >= PS_END_BREATH ? PS_NEND - 2u : PS_NEND - 1u));
        if (s.be >= PS_END_BREATH && k >= s.be)
            k++;
    }
    s.be = (uint8_t)k;
    ps_ui.build_end = (int8_t)k;
    float u = vary_u(s.builds++, 0xB111u);   /* its length swayed: longer as the rest (p/2), shorter a third as often */
    int d = u < VARY_P / 6.0f ? -1 : u >= 0.5f * VARY_P && u < VARY_P ? 1 : 0;
    enter(SEC_BUILD, BUILD_BARS[clampi(par[P_BUILD_LEN] + d, 0, 2)] +
                     (k == PS_BUILD_FAKE));   /* (FAKE: its false drop's bar) */
}

static void drop(uint32_t bar)         /* on this bar's first tick: the groove again, a shade other */
{
    enter(SEC_MAIN, 0);
    s.origin = s.last_drop = bar;
    s.drops++;
    {                                  /* another of the bass's colours; or, less often the higher DRIFT, the same */
        uint32_t n = ntypes(PART_BASS), r = rnd() % (n - 1u);
        if (vary_u(0u, 0xBA55u) >= 0.4f * (1.0f - m.drift))
            s.type[PART_BASS] = (uint8_t)((s.type[PART_BASS] + 1u + r) % n);
    }
    s.out = 0;
    ps_ui.sitting_out = 0;
    kit_morph(1.0f);                   /* (MORPH: now and then the kit too) */
    s.x_t = 0.0f;                      /* the crash */
    ps_ui.hits[HIT_PERC]++;
}

static void section_bar(const world_t *w, uint32_t bar)   /* a bar begins: the cues, and what comes on its own */
{
    uint32_t cue = cue_n, dn = drop_n, bn = build_n;
    int brk = cue != s.cue_seen, now = dn != s.drop_seen, build = bn != s.build_seen;
    s.cue_seen = cue;
    s.drop_seen = dn;
    s.build_seen = bn;
    s.sec_bar++;
    if (now) {
        drop(bar);
        return;
    }
    if (s.sec == SEC_MAIN) {
        uint32_t rb = bar - s.origin;
        if (build)                     /* a build straight from the groove */
            enter_build(w);
        else if (brk)
            enter(SEC_BREAK, 0);
        else if (par[P_BREAKS] > 0 && rb && rb % 32u == 0u && bar - s.last_drop >= GAP_BARS[clampi(par[P_GAP] + vary_step(0u, 0x6A90u, VARY_P), 0, 3)] &&
                 (float)(rnd() % 100u) < 70.0f * m.drift * ((float)par[P_BREAKS] / 50.0f)) {
            int k = clampi(par[P_BREAK_LEN], 0, 4), d = vary_step(0u, 0xB7EAu, VARY_P);   /* (swayed as BUILD's) */
            uint32_t len = k ? BREAK_BARS[clampi(k + d, 1, 4)]   /* (the world's: half or twice it, 4 to 32) */
                             : (uint32_t)clampi(d < 0 ? w->brk / 2 : d > 0 ? w->brk * 2 : w->brk, 4, 32);
            enter(SEC_BREAK, len);
        }
        if (s.sec == SEC_MAIN && rb && rb % 32u == 0u)   /* a long groove: MORPH now and then, half as likely */
            kit_morph(0.5f);
    } else if (s.sec == SEC_BREAK) {
        if (build || (s.sec_len && s.sec_bar >= s.sec_len))   /* (a break cued in a break: nothing) */
            enter_build(w);
    } else if (s.sec_bar >= s.sec_len) {
        drop(bar);
    }
}

static void bass_note(const world_t *w, uint32_t span)   /* the bass strikes, its room span ticks */
{
    s.b_t = 0.0f;
    s.b_cph = s.b_mph = 0.0f;          /* every note from the same place: the roll is tight */
    s.b_gate = w->gate * cf.b_len * s.tick_len * (float)span / PS_SR;   /* (LENGTH) */
    {   /* room for its 3 ms release before the next */
        float most = fm_minf(0.95f * s.tick_len * (float)span / PS_SR, s.tick_len * (float)span / PS_SR - 0.0035f);
        if (s.b_gate > most)
            s.b_gate = most;
    }
    s.b_inc = note_hz(m.root + s.lift + (w->jump && rnd() % 100u < w->jump ? ((rnd() & 1u) ? 12 : 7) : 0)) / PS_SR;
    ps_ui.hits[HIT_BASS]++;
}

static void step(const world_t *w)     /* a tick begins: what strikes on it */
{
    uint32_t t = s.tick % TPB, beat = s.tick / TPB % 4u, bar = s.tick / TPBAR, rb;
    int groove, on, bass_rests, kick_rests, bass;
    uint32_t kf;
    if (s.tick % TPBAR == 0u)
        section_bar(w, bar);
    {   /* STUTTER: the music repeated, a quarter, then an eighth, a sixteenth, a 32nd of it (the buffer's, fx.c) */
        static const uint8_t SL[4] = {24, 12, 6, 3};
        fx.auto_slice = s.sec == SEC_BUILD && s.bk == PS_BUILD_STUTTER && !build_breath(beat)
                      ? SL[clampi((int)((s.sec_bar * 4u + beat) / build_len()), 0, 3)] : 0u;
    }
    {   /* STAIRS: a minor third higher each quarter of it; GATE: all of it cut, quarters to 32nds (not under TAPE's
         * or REVERSE's ending) */
        static const uint8_t SL[4] = {24, 12, 6, 3};
        uint32_t q = (uint32_t)clampi((int)((s.sec_bar * 4u + beat) / build_len()), 0, 3);
        s.lift = (int8_t)(s.sec == SEC_BUILD && s.bk == PS_BUILD_STAIRS ? 3u * q : 0u);
        fx.auto_gate = (uint8_t)(s.sec == SEC_BUILD && s.bk == PS_BUILD_GATE && !build_breath(beat) &&
                                 !(build_end(beat) && (s.be == PS_END_TAPE || s.be == PS_END_REVERSE)) ? SL[q] : 0u);
        if (fx.auto_gate && s.tick % fx.auto_gate == 0u) {
            fx.ag_el = 0.0f;           /* (a slice begins) */
            fx.ag_len = fx.auto_gate;
        }
    }
    /* the ending's: TAPE, REVERSE the buffer's (from its first beat's tick); ECHO thrown into the echo, as DUB */
    fx.auto_buf = (uint8_t)(!build_end(beat) ? 0 : s.be == PS_END_TAPE ? FX_TAPE : s.be == PS_END_REVERSE ? FX_REVERSE : 0);
    fx.auto_dub = (uint8_t)(build_end(beat) && s.be == PS_END_ECHO);
    if (t % 6u == 0u)
        fx_sixteenth(t == 0u);
    if (t == 0u) {                     /* a fill cued (a black key): this beat */
        s.fill_beat = fx.fill_req;
        fx.fill_req = 0;
    }
    rb = bar - s.origin;
    groove = s.sec == SEC_MAIN;
    on = groove || (build_grooves() && !build_breath(beat)) || (build_fake() && beat == 0u);   /* the kick and the bass */
    bass_rests = !on || fill_now(rb, beat) || s.fill_beat || (!groove && !filter_in(PART_BASS, beat));
    kick_rests = !on;
    kf = beat == 3u ? kick_fill(rb) : KF_NONE;   /* a twist on the last beat: DOUBLE, ROLL */
    perc_step(w, t, beat, bar, rb, groove && bass_rests && kf != KF_ROLL);   /* (ROLL: the kick's the fill) */
    if (kf == KF_ROLL)
        bass_rests = 1;
    ev_step(w, t, rb, (uint32_t)((s.tick - s.origin * TPBAR) / 6u), (bar < 4u && !s.drops) || s.sec == SEC_BUILD);
    ln_step(w, t, bar, rb, (uint32_t)((s.tick - s.origin * TPBAR) / 6u));
    if (t == 0u && beat == 0u && rb % 8u == 0u && groove) {   /* a phrase: the bass's shape, now and then another */
        uint32_t r = rnd() % 100u;
        s.pat = (uint8_t)(rb == 0u || r >= (uint32_t)(w->gallop + w->off) ? 0 : r < w->gallop ? 1 : 3);
        r = rnd() % 100u;              /* a bar of triplets: the half's end, the 4th bar or the 8th */
        s.trip_at = (uint8_t)(rb != 0u && r < w->trip ? (r & 1u ? 3u : 7u) : 255u);
        s.kf = KF_NONE;                /* the kick's twist as it ends (not in the intro, nor where the 32nd bar's is) */
        if (bar >= 16u && rb != 0u && par[P_FILLS] && rb % 32u != 24u) {
            static const float F[5] = {0.0f, 0.4f, 0.7f, 1.0f, 1.4f};
            float f = F[clampi(par[P_FILLS], 0, 4)], half = w->bpm >= 168.0f ? 40.0f : 20.0f;
            float u = (float)(rnd() % 1000u);
            s.kf = (uint8_t)(u < 80.0f * f ? KF_DOUBLE : u < 130.0f * f ? KF_ROLL : u < 160.0f * f ? KF_BARE
                             : u < (160.0f + half) * f ? KF_HALF : KF_NONE);
            s.kf_bars = (uint8_t)(1u + (rnd() & 1u));
        }
    }
    if (t == 0u && beat == 0u && shape_want != PS_SHAPE_AUTO) {   /* a shape held (GROOVE's ALGORITHM): from this bar */
        static const uint8_t PAT[PS_NSHAPE] = {0, 0, 1, 3};
        s.pat = PAT[shape_want & 3];
        s.trip_at = 255;
    }
    ps_ui.section = (int8_t)s.sec;
    ps_ui.sec_bar = (uint8_t)(groove ? rb % 8u : s.sec_bar);
    ps_ui.sec_len = (uint8_t)(s.sec == SEC_BUILD ? s.sec_len : 0u);
    ps_ui.drops = s.drops;
    ps_ui.cued = (int8_t)(drop_n != s.drop_seen     ? PS_CUE_DROP
                          : build_n != s.build_seen ? PS_CUE_BUILD
                          : cue_n != s.cue_seen && s.sec == SEC_MAIN ? PS_CUE_BREAK : 0);
    if (t == 0u && beat == 3u && fx.gap_req) {   /* GAP (a black key): this bar's last beat silent */
        fx.gap_req = 0;
        s.gap = 1;
    } else if (t == 0u && beat == 0u)
        s.gap = 0;
    if (s.be == PS_END_PICKUP && build_end(beat) && t == 18u) {   /* PICKUP: its last sixteenth, the kick and the bass */
        s.k_t = 0.0f;
        s.k_ph = 0.0f;
        ps_ui.hits[HIT_KICK]++;
        bass_note(w, 6u);
        return;
    }
    if (acid_build()) {                /* ACID: the bass alone, every sixteenth, an octave up every third (its breath:
                                        * none) */
        if (t % 6u == 0u && !build_breath(beat)) {
            bass_note(w, 6u);
            if ((s.sec_bar * 16u + beat * 4u + t / 6u) % 3u == 2u)
                s.b_inc *= 2.0f;
        }
        return;
    }
    if (fx.half || half_build()) {     /* HALF (a black key held, or a build): half time, the kick on 1 and 3, the bass's roll over
                                        * two beats (K-B-B-B, an eighth each); the kick's twists not */
        uint32_t ht = (beat & 1u) * TPB + t;
        if (ht == 0u) {
            if (!kick_rests) {
                s.k_t = 0.0f;
                s.k_ph = 0.0f;
                ps_ui.hits[HIT_KICK]++;
            }
        } else if (ht % 12u == 0u && !bass_rests && !(w->skip && rnd() % 100u < w->skip))
            bass_note(w, 12u);
        return;
    }
    if (t == 0u || (t % 6u == 0u && kf != KF_NONE && kick_at(rb, beat, t))) {   /* (a twist's: its tick the kick's) */
        if (!kick_rests && kick_at(rb, beat, t)) {
            s.k_t = 0.0f;
            s.k_ph = 0.0f;
            ps_ui.hits[HIT_KICK]++;
        }
        return;
    }
    {
        uint32_t pat = kf == KF_DOUBLE ? 0u : bass_pat(rb), roll = pat == 2u ? 4u : 6u;   /* (DOUBLE: K-B-B-K) */   /* triplets: K-b-b-b-b-b; a gallop: K-.-B-B; an
                                                                    * offbeat: K-.-B-. */
        uint32_t span = pat == 3u ? 12u : roll;   /* the room a note has (the offbeat's: to the next kick) */
        bass = t % roll == 0u && !(pat == 1u && t == 6u) && !(pat == 3u && t != 12u);
        if (!bass || bass_rests)
            return;
        if (w->skip && rnd() % 100u < w->skip)   /* a stutter: this one rests */
            return;
        bass_note(w, span);
    }
}

static float kick(float dt)
{
    float f0, f, env, x, t = s.k_t, punch = (float)par[P_KICK_PUNCH] / 50.0f * cf.kick_bright;
    const float *ty;
    if (t < 0.0f)
        return 0.0f;
    if (t == 0.0f) {                                /* its type and DRIVE, as it strikes */
        s.k_ty = KICK_TYPE[s.type[PART_KICK]];
        s.k_mph = 0.0f;
        s.k_dr = s.k_ty[KT_DRIVE];
        s.k_cmp = 1.0f;
        if (cf.k_drive != 1.0f) {                   /* its peak kept the type's (and driven harder, denser: a little
                                                     * under it, the loudness about the same) */
            s.k_dr *= cf.k_drive;
            s.k_cmp = fm_tanhf(s.k_ty[KT_DRIVE]) / fm_tanhf(s.k_dr);
            if (cf.k_drive > 1.0f)
                s.k_cmp *= fm_powf(cf.k_drive, -0.32f);
        }
    }
    ty = s.k_ty;
    if (t >= s.k_len) {
        s.k_t = -1.0f;
        return 0.0f;
    }
    f0 = cf.kick_f0;                                /* tuned to the key */
    f = f0 + f0 * (4.0f + 5.0f * punch) * ty[KT_SWEEP] * fm_expf(-t / ty[KT_FALL]);   /* the pitch falls: Punch, from how high */
    s.k_ph += f * dt;
    if (s.k_ph >= 1.0f)
        s.k_ph -= 1.0f;
    env = fm_expf(-t / (s.k_len * ty[KT_BODY] * cf.k_body));   /* (DECAY: the body; its end stays the bass's) */
    env *= 1.0f - (cf.k_fade == 1.0f ? t / s.k_len : fm_powf(t / s.k_len, cf.k_fade));   /* to nothing by its end: the
                                                     * bass's place stays clear (DECAY over 50: later) */
    if (t < 0.0002f)                                /* its first 0.2 ms a slope, not a step (the click too) */
        env *= t / 0.0002f;
    if (ty[KT_FM] > 0.0f) {                         /* KNOCK: a modulator a little off the note, its depth falling fast */
        float mo = fm_sin_turns(s.k_mph) * ty[KT_FM] * fm_expf(-t / 0.004f) * 0.159155f;
        if ((s.k_mph += f * 1.53f * dt) >= 1.0f)
            s.k_mph -= 1.0f;
        x = fm_sin_turns(s.k_ph + mo) * env;
    } else
        x = fm_sin_turns(s.k_ph) * env;
    x += (t < 0.002f ? 0.35f * punch * ty[KT_CLICK] * (1.0f - t / 0.002f) : 0.0f) * fm_sin_turns(s.k_ph * 7.0f) *   /* the click */
         (t < 0.0002f ? t / 0.0002f : 1.0f);
    if (ty[KT_NOISE] > 0.0f && t < 0.012f) {       /* CRACK: noise, its lows taken off, gone in a few ms (its own dice) */
        float n;
        s.k_nz = s.k_nz * 1664525u + 1013904223u;
        n = (float)(int32_t)s.k_nz * (1.0f / 2147483648.0f);
        s.k_nb += 0.45f * (n - s.k_nb);           /* (its very top rounded: ~4 kHz up, not a sample's steps) */
        x += 0.6f * ty[KT_NOISE] * (0.6f + 0.4f * punch) * (s.k_nb - s.k_nl) * fm_expf(-t / 0.0022f) *
             (t < 0.0005f ? t / 0.0005f : 1.0f);
        s.k_nl = s.k_nb;
    }
    s.k_t = t + dt;
    x = fm_tanhf(x * s.k_dr) * ty[KT_LEVEL] * s.k_cmp;
    return cf.k_cut > 0.0f ? x * fm_expf(-t * cf.k_cut) : x;   /* (DECAY under 50: cut short after the drive) */
}

/* the bass's colours: ratio, bite, how fast the bite closes, and a blip of pitch as it strikes (octaves, gone in ~3 ms)
 *   ROUND BITTEN METAL HOLLOW  as they were
 *   SNAP   bitten hard and closing four times as fast, the pitch knocked up an instant: a click on each note, round
 *          under it
 *   NASAL  the modulator an octave over the note: odd harmonics only, a hollow buzz */
static const float COLOUR[PS_NTYPE][4] = {{1.0f, 1.0f, 1.0f, 0.0f}, {1.0f, 1.35f, 1.0f, 0.0f}, {1.5f, 0.8f, 1.0f, 0.0f},
                                          {0.5f, 1.25f, 1.0f, 0.0f}, {1.0f, 1.9f, 4.0f, 0.6f}, {2.0f, 0.95f, 1.0f, 0.0f}};

static float bass(const world_t *w, float dt)
{
    float t = s.b_t, idx, env, x, mo, bite = (float)par[P_BASS_BITE] / 50.0f * COLOUR[s.type[PART_BASS]][1];
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
    idx = idx * fm_expf(-t * cf.b_idx_k * COLOUR[s.type[PART_BASS]][2] * (1.0f - 0.8f * cf.acid)) +
          w->floor * bite * cf.b_floor;   /* (DECAY: how fast it closes; ACID: slower, staying open) */
    idx *= 1.0f + 2.5f * cf.acid;      /* (ACID: opening, screaming) */
    mo = fm_sin_turns(s.b_mph);
    x = fm_sin_turns(s.b_cph + idx * mo * 0.159155f) * env;
    {
        float inc = s.b_inc, blip = COLOUR[s.type[PART_BASS]][3];
        if (blip > 0.0f && t < 0.015f)             /* SNAP: the pitch knocked up as it strikes */
            inc *= fm_exp2f(blip * fm_expf(-t / 0.003f));
        s.b_cph += inc;
        if (s.b_cph >= 1.0f)
            s.b_cph -= 1.0f;
        s.b_mph += inc * w->ratio * COLOUR[s.type[PART_BASS]][0];
    }
    if (s.b_mph >= 1.0f)
        s.b_mph -= 1.0f;
    if (s.b_t >= 0.0f)
        s.b_t = t + dt;
    x = fm_tanhf(x * w->drive * (0.5f + (float)par[P_BASS_DRIVE] / 100.0f) * (1.0f + 1.5f * cf.acid));
    s.lp += (x - s.lp) * (cf.b_lp + (1.0f - cf.b_lp) * cf.acid);   /* (Dark: darker; ACID: opening) */
    return s.lp;
}

#define MASTER_TRIM 0.65f
static float soft(float x)             /* as it is below 0.7, then rounding off towards 1 (never past it) */
{
    float a = fm_fabsf(x), y;
    if (a <= 0.7f)
        return x;
    y = 0.7f + 0.3f * fm_tanhf((a - 0.7f) / 0.3f);
    return x < 0.0f ? -y : y;
}

static float noise(void) { return (float)(int32_t)rnd() * (1.0f / 2147483648.0f); }

/* the percussion, a sample: l and r added to */
static void perc(const world_t *w, float dt, float *l, float *r)
{
    const float *pt = PERC_TYPE[s.type[PART_PERC]];
    float n = noise(), hp, x, a, src, cn;
    int i;
    for (i = 0; i < 4; i++)            /* the source, moving to the type's over ~3 ms (the hats ring through it) */
        s.p_w[i] += (pt[PT_NOISE + i] - s.p_w[i]) * 0.008f;
    a = fm_clampf(w->hat_hp - cf.p_thin - 0.05f * m.dark + pt[PT_THIN], 0.4f, 0.97f);   /* (TONE over 50: thinner) */
    src = n * s.p_w[0];
    if (s.p_w[1] > 0.0005f) {          /* TIN: six squares, inharmonic */
        static const float SQ[6] = {307.9f, 456.6f, 554.4f, 784.0f, 810.0f, 1200.0f};
        float sq = 0.0f;
        for (i = 0; i < 6; i++) {
            if ((s.p_ph[i] += SQ[i] / PS_SR) >= 1.0f)
                s.p_ph[i] -= 1.0f;
            sq += s.p_ph[i] < 0.5f ? 1.0f : -1.0f;
        }
        src += sq * (1.0f / 6.0f) * s.p_w[1];
    }
    if (s.p_w[2] > 0.0005f) {          /* GRIT: the noise held six samples (about 7 kHz) */
        if (s.p_hc++ % 6u == 0u)
            s.p_nh = n;
        src += s.p_nh * s.p_w[2];
    }
    cn = (n * (1.0f - s.p_w[2]) + s.p_nh * s.p_w[2]) * s.p_w[3];   /* the clap's noise: GRIT's too */
    src *= s.p_w[3];
    /* the hats: noise through two high-passes */
    s.h_hp1 = a * (s.h_hp1 + src - s.h_x1);
    s.h_x1 = src;
    s.h_hp2 = a * (s.h_hp2 + s.h_hp1 - s.h_x2);
    s.h_x2 = s.h_hp1;
    hp = s.h_hp2;
    s.p_sw += ((pt[PT_ATTACK] > 0.0f ? 1.0f : 0.0f) - s.p_sw) * 0.008f;
    if (s.p_sw > 0.0005f) {            /* SEED: the hats from a band round 4.5 kHz instead */
        float hi = src - s.p_slow - 0.4f * s.p_sband;   /* (~4.5 kHz, narrow) */
        s.p_sband += 0.63f * hi;
        s.p_slow += 0.63f * s.p_sband;
        hp += s.p_sw * (0.6f * s.p_sband - hp);
    }
    if (s.h_t == 0.0f) {               /* (each takes its type's decay as it strikes) */
        s.h_k = pt[PT_HAT];
        s.h_a = fm_maxf(pt[PT_ATTACK], 0.0001f);
    }
    if (s.o_t == 0.0f) {
        s.o_k = pt[PT_OPEN] * cf.o_len;  /* (OPEN: longer or shorter) */
        s.o_a = fm_maxf(pt[PT_ATTACK], 0.0001f);
    }
    if (s.p_bc++ % 40u == 0u)          /* SEED's grain: each ~0.9 ms a seed or nearly none, by the noise's size (the
                                        * music's dice as they are), */
        s.p_bt = fm_fabsf(n) > 0.55f ? 1.7f : 0.2f;
    s.p_bead += (s.p_bt - s.p_bead) * 0.03f;   /* reached in ~0.8 ms */
    if (s.c_t == 0.0f)
        s.c_k = pt[PT_CLAP];
    if (s.r_t == 0.0f)
        s.r_k = pt[PT_RIDE];
    if (s.h_t >= 0.0f) {
        x = hp * s.h_v * fm_expf(-s.h_t * 1000.0f / (w->hat_ms * s.h_k)) * 0.20f * (s.h_t < s.h_a ? s.h_t / s.h_a : 1.0f);
        if (s.h_a > 0.001f)            /* (SEED: swelling slower at first, grainy) */
            x *= (s.h_t < s.h_a ? s.h_t / s.h_a : 1.0f) * s.p_bead;
        *l += 0.8f * x;
        *r += 1.1f * x;
        if ((s.h_t += dt) > w->hat_ms * s.h_k * 0.008f)
            s.h_t = -1.0f;
    }
    if (s.o_t >= 0.0f) {
        x = hp * s.o_v * fm_expf(-s.o_t * 1000.0f / (w->open_ms * s.o_k)) * 0.22f * (s.o_t < s.o_a ? s.o_t / s.o_a : 1.0f);
        if (s.o_a > 0.001f)
            x *= (s.o_t < s.o_a ? s.o_t / s.o_a : 1.0f) * s.p_bead;
        *l += 1.05f * x;
        *r += 0.85f * x;
        if ((s.o_t += dt) > w->open_ms * s.o_k * 0.008f)
            s.o_t = -1.0f;
    }
    /* the clap: a band of noise, three quick bursts, then its tail */
    if (s.c_t >= 0.0f) {
        float t = s.c_t, f = cf.clap_f, hi, env;
        (void)w;
        hi = cn - s.c_low - 0.9f * s.c_band;
        s.c_band += f * hi;
        s.c_low += f * s.c_band;
        if (t < 0.024f) {
            float u = t - 0.008f * fm_floorf(t / 0.008f);
            env = fm_expf(-u / 0.0025f) * (u < 0.0001f ? u / 0.0001f : 1.0f);   /* (each burst from a slope) */
        } else {
            float u = t - 0.024f;      /* (from a slope as the bursts are: no step out of the third) */
            env = 0.8f * fm_expf(-u / (0.045f * s.c_k)) * (u < 0.0001f ? u / 0.0001f : 1.0f);
        }
        x = s.c_band * env * s.c_v * 1.4f * (t < 0.0001f ? t / 0.0001f : 1.0f);   /* (its first 0.1 ms a slope) */
        *l += x;
        *r += x;
        if ((s.c_t += dt) > 0.3f)
            s.c_t = -1.0f;
    }
    /* the ride: three inharmonic partials and a little of the hats' noise */
    if (s.r_t >= 0.0f) {
        static const float RH[3] = {3120.0f, 4617.0f, 7394.0f};
        float mo = 0.0f, env = fm_expf(-s.r_t / (0.16f * s.r_k));
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

/* the build's riser: a band of noise rising over its bars, gone for the breath; returns what goes to the echo */
static float build_progress(void)      /* 0..1 through a build's bars (FAKE's last: 1) */
{
    uint32_t tk = s.tick ? s.tick - 1u : 0u;   /* (the tick sounding: s.tick is the next's) */
    return s.sec == SEC_BUILD ? fm_minf(((float)s.sec_bar + (float)(tk % TPBAR) / (float)TPBAR) / (float)build_len(), 1.0f)
                              : 0.0f;
}

static float riser(float n, float *l, float *r)
{
    float p = build_progress(), target = 0.0f, f, x;
    uint32_t beat = (s.tick ? s.tick - 1u : 0u) / TPB % 4u;
    if (s.sec == SEC_BUILD && !(build_breath(beat) && (s.be != PS_END_VOID || beat == 3u)) && !build_fake())
        target = p * p;   /* (VOID's: on through its bar, but its last beat) */
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
    float lvl = (float)par[P_ATMOS_LEVEL] / 70.0f, target = playing ? (s.sec == SEC_MAIN ? w->atmos : 1.0f) * lvl : 0.0f; /* (resting: it fades away) */
    float colour = (float)par[P_ATMOS_COLOUR] / 100.0f, air = (float)par[P_ATMOS_AIR] / 100.0f, wob, al = 0.0f, ar = 0.0f, k, x;
    int i;
    s.a_lvl += (target - s.a_lvl) * (target > s.a_lvl ? 1.0f / (2.5f * PS_SR) : 1.0f / (1.5f * PS_SR));
    if (s.a_lvl < 0.0005f)
        return 0.0f;
    colour += 0.4f * build_progress(); /* it opens as the build rises */
    if ((s.a_lfo += cf.a_rate) >= 1.0f)   /* MOTION: how fast it sways (50: 0.05 Hz) */
        s.a_lfo -= 1.0f;
    wob = fm_sin_turns(s.a_lfo) * (float)par[P_ATMOS_DEPTH] / 50.0f * (1.0f + 0.6f * cf.a_more);   /* DEPTH: how far (0..2,
                                                     * 50: 1; MOTION past 50: more) */
    {   /* the type: each of its values moving to the type's over ~45 ms (a held chord: no step) */
        const float *tt = ATMOS_TYPE[s.type[PART_ATMOS]];
        for (i = 0; i < AM_N; i++)
            s.a_m[i] += (tt[i] - s.a_m[i]) * 0.0005f;
    }
    for (i = 0; i < 3; i++) {          /* the chord: each note a little apart left and right, its modulator shared
                                        * (lighter: its root alone, the others fading over ~20 ms, their phases on) */
        float inc = cf.a_inc[i], idx = (0.15f + 1.2f * colour) * (0.75f + 0.25f * wob) * s.a_m[AM_IDX], mo,
              g = (s.a_vg[i] += ((i && shed ? 0.0f : 1.0f) - s.a_vg[i]) * 0.0012f);
        if (g > 0.0005f) {
            mo = fm_sin_turns(s.a_mph[i]) * idx * 0.159155f;
            al += fm_sin_turns(s.a_ph[i][0] + mo) * g;
            ar += fm_sin_turns(s.a_ph[i][1] + mo) * g;
        }
        if ((s.a_mph[i] += s.a_m[AM_RATIO] * inc) >= 1.0f)
            s.a_mph[i] -= 1.0f;
        if ((s.a_ph[i][0] += inc * (1.0f - s.a_m[AM_DET] * (float)(i + 1))) >= 1.0f)
            s.a_ph[i][0] -= 1.0f;
        if ((s.a_ph[i][1] += inc * (1.0f + s.a_m[AM_DET] * (float)(i + 1))) >= 1.0f)
            s.a_ph[i][1] -= 1.0f;
    }
    k = fm_minf(fm_clampf(0.03f + 0.35f * colour, 0.02f, 0.6f) * s.a_m[AM_LP] *
                (cf.a_more > 0.0f ? fm_exp2f(0.6f * cf.a_more * wob) : 1.0f), 0.9f);   /* (MOTION past 50: opening and
                                                                                        * closing with the sway) */
    s.a_lp[0] += (al - s.a_lp[0]) * k;
    s.a_lp[1] += (ar - s.a_lp[1]) * k;
    {   /* the air: a band of noise, wandering, swaying left and right */
        float f = cf.air_f;
        s.a_band += f * (n - s.a_low - 0.7f * s.a_band);
        s.a_low += f * s.a_band;
        x = s.a_band * air * 0.5f * s.a_m[AM_AIR];
    }
    {   /* GRAIN: the chord in grains, 15..60 ms each, each at its own level (often none), their edges ~2 ms */
        float g = s.a_m[AM_GRAIN], gate;
        if (g > 0.0005f) {
            if (s.a_gn == 0u) {
                s.a_rng = s.a_rng * 1664525u + 1013904223u;
                s.a_gn = 660u + (s.a_rng >> 8) % 2000u;
                s.a_gtar = (s.a_rng >> 24) < 90u ? 0.0f : (float)((s.a_rng >> 16) & 255u) / 170.0f;   /* (to 1.5: the gaps' level back) */
            }
            s.a_gn--;
            s.a_genv += (s.a_gtar - s.a_genv) * 0.01f;
        }
        gate = 1.0f - g + g * s.a_genv;
        if (s.a_m[AM_PULSE] > 0.0005f) {   /* PULSE: on the sixteenths of 1101 1010 1101 1011, each its first 60% */
            uint32_t q = s.tick % TPBAR, st = q * 16u / TPBAR, in = q * 16u % TPBAR;
            float on = (0xDADBu >> (15u - st) & 1u) && in < TPBAR * 6u / 10u ? 1.0f : 0.0f;
            s.a_pg += (on - s.a_pg) * 0.006f;
            gate *= 1.0f - s.a_m[AM_PULSE] + s.a_m[AM_PULSE] * 1.7f * s.a_pg;   /* (the gaps' level back) */
        }
        al = s.a_lp[0] * 0.10f * gate + x * (0.6f + fm_clampf(0.35f * wob, -0.6f, 0.6f));
        ar = s.a_lp[1] * 0.10f * gate + x * (0.6f - fm_clampf(0.35f * wob, -0.6f, 0.6f));
    }
    *l += al * s.a_lvl;
    *r += ar * s.a_lvl;
    return (al + ar) * s.a_lvl * (float)par[P_ATMOS_WASH] / 100.0f;
}

static void flush_states(void)         /* as a block begins: what decays towards nothing reaches it (no denormals) */
{
    s.lp = fm_flush(s.lp);
    s.z_g = fm_flush(s.z_g), s.z_low = fm_flush(s.z_low), s.z_band = fm_flush(s.z_band);
    s.a_lvl = fm_flush(s.a_lvl), s.a_lp[0] = fm_flush(s.a_lp[0]), s.a_lp[1] = fm_flush(s.a_lp[1]);
    s.e_lp[0] = fm_flush(s.e_lp[0]), s.e_lp[1] = fm_flush(s.e_lp[1]);
    s.c_low = fm_flush(s.c_low), s.c_band = fm_flush(s.c_band), s.p_lp[0] = fm_flush(s.p_lp[0]), s.p_lp[1] = fm_flush(s.p_lp[1]);
    ln.lp = fm_flush(ln.lp);
    fx.wet = fm_flush(fx.wet), fx.lo_g = fm_flush(fx.lo_g), fx.hi_g = fm_flush(fx.hi_g);
    fx.band_g = fm_flush(fx.band_g), fx.fl_g = fm_flush(fx.fl_g), fx.riser_g = fm_flush(fx.riser_g), fx.gate_g = fm_flush(fx.gate_g);
    fx.wash_g = fm_flush(fx.wash_g), fx.dub_g = fm_flush(fx.dub_g), fx.frz_g = fm_flush(fx.frz_g), fx.ret_lift = fm_flush(fx.ret_lift);
}

static void coefs(const world_t *w)    /* as a block begins: what changes slowly */
{
    static const int8_t IV[2][3] = {{24, 31, 36}, {24, 30, 25}};   /* the atmos over the root: fifths; dark, a tritone and a second */
    float colour = (float)par[P_ATMOS_COLOUR] / 100.0f + 0.4f * build_progress();
    int i;
    float bp = s.sec == SEC_BUILD ? build_progress() : 0.0f;
    cf.kick_f0 = note_hz(m.root + par[P_KICK_TUNE]) * (s.bk == PS_BUILD_KICK ? fm_exp2f(bp) : 1.0f);   /* (KICK: an
                                                                                                       * octave up) */
    cf.kick_bright = w->kick_bright;
    cf.clap_f = 2.0f * fm_sinf(3.14159265f * w->clap_hz * fm_exp2f((float)(par[P_HAT_TONE] - 50) / 50.0f * 0.8f) *   /* (TONE:
                                                     * x0.57 .. x1.74; TRIPLET: rising; SINK: falling an octave) */
                               (s.bk == PS_BUILD_TRIP ? fm_exp2f(1.3f * bp) : s.bk == PS_BUILD_SINK ? fm_exp2f(-bp) : 1.0f) / PS_SR);
    cf.z_f = 2.0f * fm_sinf(3.14159265f * 300.0f *   /* 300 Hz to 6.8 kHz (SINK: down) */
                            fm_exp2f(4.5f * (s.bk == PS_BUILD_SINK && s.sec == SEC_BUILD ? 1.0f - bp : bp)) / PS_SR);
    cf.lp_f = 2.0f * fm_sinf(3.14159265f * 6000.0f * fm_exp2f(-4.3f * bp) / PS_SR);   /* SINK: 6 kHz down to ~300 Hz */
    cf.air_f = 2.0f * fm_sinf(3.14159265f * (1200.0f + 900.0f * fm_sin_turns(s.a_lfo) + 1500.0f * colour) / PS_SR);
    for (i = 0; i < 3; i++)
        cf.a_inc[i] = note_hz(m.root + s.lift + IV[m.dark > 0.3f][i]) / PS_SR;
    cf.acid = acid_build() ? bp : 0.0f;
    {   /* the kick's DECAY: its body x0.4 .. x2.5; under 50 cut short after its drive too (its drive flattens the body:
         * the cut is heard), over 50 its end's fade held off (fuller to the bass) */
        float d = (float)(par[P_KICK_DECAY] - 50) / 50.0f;
        cf.k_body = fm_exp2f(d * 1.3f);
        cf.k_cut = d < 0.0f && s.k_len > 0.0f ? -d * 7.0f / s.k_len : 0.0f;
        cf.k_fade = d > 0.0f ? 1.0f + 3.0f * d : 1.0f;
    }
    cf.b_lp = fm_clampf(0.45f - 0.2f * m.dark, 0.06f, 0.95f);
    {   /* the percussion's TONE: 0 a low-pass at 1.8 kHz over all of it, 50 none, 100 the hats thinner (and the clap's
         * band higher: cf.clap_f) */
        int v = par[P_HAT_TONE];
        cf.p_thin = v > 50 ? 0.007f * (float)(v - 50) : 0.0f;
        cf.p_lp = v < 50 ? 1.0f - fm_expf(-6.2831853f * 1800.0f * fm_exp2f(3.3f * (float)v / 50.0f) / PS_SR) : 1.0f;
    }
    {   /* the bass's DECAY: how long its bite stays, of the world's: x0.25 .. x5 */
        float d = (float)(par[P_BASS_DECAY] - 50) / 50.0f;
        cf.b_idx_k = 1000.0f / (w->index_ms * fm_exp2f(d * (d < 0.0f ? 2.0f : 2.3f)));
        cf.b_floor = d < 0.0f ? 1.0f + 0.7f * d : 1.0f;   /* (under 50 what stays of it less too: a rounder body) */
    }
    {   /* the bass's LENGTH: 0 a quarter of the world's, 50 the world's, 100 as long as a note can be (0.95) */
        int v = par[P_BASS_LEN];
        cf.b_len = v <= 50 ? fm_exp2f((float)(v - 50) / 25.0f) : 1.0f + (0.95f / w->gate - 1.0f) * (float)(v - 50) / 50.0f;
    }
    cf.o_len = fm_exp2f((float)(par[P_PERC_OPEN] - 50) / 25.0f);   /* x0.25 .. x4 */
    cf.l_drive = par[P_LINE_DRIVE] ? 1.0f + 0.08f * (float)par[P_LINE_DRIVE] : 0.0f;   /* x1 .. x9 into a tanh */
    cf.l_cmp = cf.l_drive > 0.0f ? fm_powf(cf.l_drive, -0.3f) / fm_tanhf(cf.l_drive) : 1.0f;
    {   /* MOTION: 0.0125 .. 0.05 Hz (50) .. 0.8 Hz; past 50 the sway deeper and opening and closing it too */
        int v = par[P_ATMOS_MOTION];
        cf.a_rate = 0.05f * fm_exp2f((float)(v - 50) / (v <= 50 ? 25.0f : 12.5f)) / PS_SR;
        cf.a_more = v > 50 ? (float)(v - 50) / 50.0f : 0.0f;
    }
    fx_block(s.tick_len);
}

static void tempo(const world_t *w)
{
    mods(w);
    s.tick_len = PS_SR * 60.0f / m.bpm / (float)TPB;
    s.k_len = w->kick_len * s.tick_len * 6.0f / PS_SR;   /* the world's (not a knob's: up to the bass is all it has) */
    {   /* DRIVE: of the type's, half (0) to three times (100); 50 as it is */
        int v = par[P_KICK_DRIVE];
        cf.k_drive = v == 50 ? 1.0f : fm_exp2f((float)(v - 50) / 50.0f * (v < 50 ? 1.0f : 1.585f));
    }
    if (s.k_len > 0.98f * s.tick_len * 4.0f / PS_SR)   /* never into the bass's first note (a triplet's, the soonest) */
        s.k_len = 0.98f * s.tick_len * 4.0f / PS_SR;
    coefs(w);
}

void ps_render(float *outl, float *outr, uint32_t n)
{
    const world_t *w = &W[world];
    float dt = 1.0f / PS_SR;
    /* the levels: targets, the gains gliding to them sample by sample (a knob turned quickly does not zip) */
    float tg[NG] = {0.85f * (float)par[P_KICK_LEVEL] / 80.0f, 0.55f * (float)par[P_BASS_LEVEL] / 80.0f,
                    (float)par[P_PERC_LEVEL] / 80.0f, (float)par[P_EV_LEVEL] / 80.0f, (float)par[P_LINE_LEVEL] / 70.0f,
                    (float)par[P_EV_ECHO] / 50.0f, 0.7f * (float)par[P_ECHO_LEVEL] / 50.0f};
    float *g = s.g, kick_g, bass_g, perc_g, ev_g, line_g, send_g, ret_g;
    float pk[NPART] = {0};             /* the parts' peaks this block (the meters) */
    float perc_send = 0.4f * (float)par[P_PERC_SEND] / 100.0f, line_send = (float)par[P_LINE_SEND] / 50.0f;
    float fb = (float)par[P_ECHO_FB] / 100.0f, tone = 0.12f + 0.46f * (float)par[P_ECHO_TONE] / 100.0f;
    uint32_t i;
    static float fx_send;              /* what the effects sent to the echo (the next frame takes it) */
    float hp_to, hp_g;                 /* FILTER's cutoff, the block's target (0: none); its share now */
    int lp_on;                         /* SINK's low-pass, the block's */
    if (want_play != playing || restart_n != restart_seen) {
        restart_seen = restart_n;
        playing = want_play;
        s.tick = 0;
        s.next = 0.0f;
        enter(SEC_MAIN, 0);            /* from the top: the groove, the phrases from bar 0, the cues forgotten */
        s.origin = s.last_drop = 0;
        s.cue_seen = cue_n;
        s.drop_seen = drop_n;
        s.build_seen = build_n;
        s.drops = 0;                   /* (the intro again: the events quiet its first bars) */
        s.builds = 0;
        s.out = s.pat = s.fill_beat = s.gap = 0;   /* (a GAP cued or sounding: forgotten) */
        fx.gap_req = 0;
        s.trip_at = 255;
        s.kf = KF_NONE;
        ev.pending = 0;                /* (keys pressed while resting are not thrown in a heap) */
        take.n = take.arm = take.rec = 0;   /* (the keys' loop gone) */
    }
    while (cmd_r != cmd_w) {           /* the keys, the effects, the parts */
        uint32_t c = cmd_q[cmd_r % CMD_N];
        cmd_r++;
        cmd_do(c);
    }
    tempo(w);
    if (!playing) {                    /* resting: no STUTTER's repeat going on */
        fx.auto_slice = fx.auto_buf = fx.auto_dub = fx.auto_gate = 0;
        if (fx.cur.id == FX_AUTO || (fx.cur.id >= 0 && fx.buf_want < 0))
            fx.buf_on = 0;
    }
    if (dub_on()) {                    /* DUB: the echo longer and darker (its time stretching: below); FREEZE: it
                                        * takes in the music for one echo's time, then keeps it (below) */
        fb = 0.9f;
        tone = fx.held >> FX_DUB & 1u ? 0.16f : fm_maxf(tone, 0.4f);   /* (an ending's throw: not so dark) */
    }
    if (fx.held >> FX_FREEZE & 1u) {
        fb = 1.0f;
        tone = 1.0f;
        send_g = 0.0f;
    }
    flush_states();
    {   /* FILTER's high-pass, its cutoff now: 30 Hz rising to ~680 Hz through the build; its breath and after, down */
        uint32_t tk = s.tick ? s.tick - 1u : 0u;
        hp_to = s.sec == SEC_BUILD && s.bk == PS_BUILD_FILTER && !build_breath(tk / TPB % 4u)
              ? 30.0f * fm_exp2f(4.5f * build_progress()) : 0.0f;
        lp_on = s.sec == SEC_BUILD && s.bk == PS_BUILD_SINK && !build_breath(tk / TPB % 4u);
    }
    if (!playing)                      /* stopped: the kit at once */
        kit_step();
    for (i = 0; i < n; i++) {
        {
            int k;
            for (k = 0; k < NG; k++)
                g[k] += (tg[k] - g[k]) * 0.002f;
            kick_g = g[0], bass_g = g[1], perc_g = g[2], ev_g = g[3], line_g = g[4], send_g = g[5], ret_g = g[6];
        }
        if (playing) {
            if (s.next <= 0.0f) {                  /* a tick begins on this sample */
                if (s.tick % TPBAR == 0u && want_world != world) {   /* a world changes as a bar begins */
                    world = want_world;
                    w = &W[world];
                    tempo(w);
                }
                if (s.tick % TPBAR == 0u)          /* the kit, as a bar begins */
                    kit_step();
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
            float dt_e = fm_minf(s.tick_len * (float)ECHO_TICKS[par[P_ECHO_TIME] & 3], (float)(ECHO_N - 2u)), ef;
            uint32_t d;
            int k;
            for (k = 0; k < NPART; k++)        /* the parts' gains glide to their mutes (~3 ms) */
                pg[k] += (((fx.muted | fx.out) >> k & 1u ? 0.0f : 1.0f) - pg[k]) * 0.008f;
            s.hp_hz += (hp_to - s.hp_hz) * 0.001f;   /* ~23 ms */
            hp_g = fm_clampf((s.hp_hz - 10.0f) / 30.0f, 0.0f, 1.0f);   /* (in and out by degrees: no step) */
            if (s.hp_hz > 10.0f) {     /* FILTER: the bass thinned from under (a Chamberlin's high, 12 dB an octave) */
                float f = 6.2831853f * s.hp_hz / PS_SR, hi = s_bass - s.hp_b[0] - s.hp_b[1];
                s.hp_b[1] += f * hi;
                s.hp_b[0] += f * s.hp_b[1];
                s_bass += (hi - s_bass) * hp_g;
            } else
                s.hp_b[0] = s.hp_b[1] = 0.0f;
            c = kick_g * pg[PART_KICK] * s_kick + bass_g * pg[PART_BASS] * s_bass;
            perc(w, dt, &l, &r);
            if (cf.p_lp < 1.0f) {      /* TONE under 50: darker */
                s.p_lp[0] += (l - s.p_lp[0]) * cf.p_lp;
                s.p_lp[1] += (r - s.p_lp[1]) * cf.p_lp;
                l = s.p_lp[0];
                r = s.p_lp[1];
            } else
                s.p_lp[0] = s.p_lp[1] = 0.0f;
            l *= pg[PART_PERC];
            r *= pg[PART_PERC];
            send = ev_render(dt, &el, &er) * ev_g * send_g * pg[PART_EVENTS] + perc_g * (l + r) * perc_send;
            el *= pg[PART_EVENTS];
            er *= pg[PART_EVENTS];
            {   /* the line: softer in a break, and more of it into the echo */
                float lg = line_g * pg[PART_LINE] * (s.sec == SEC_BREAK ? 0.6f : 1.0f);
                send += ln_render(w, dt, &ll, &lr) * lg * (s.sec == SEC_MAIN ? 0.5f : 1.2f) * line_send;
                ll *= lg;
                lr *= lg;
            }
            {   /* the meters: each part's peak as heard (the percussion's before the riser joins it) */
                float a = pg[PART_ATMOS];
                pk[PART_KICK] = fm_maxf(pk[PART_KICK], fm_fabsf(kick_g * pg[PART_KICK] * s_kick));
                pk[PART_BASS] = fm_maxf(pk[PART_BASS], fm_fabsf(bass_g * pg[PART_BASS] * s_bass));
                pk[PART_PERC] = fm_maxf(pk[PART_PERC], perc_g * fm_maxf(fm_fabsf(l), fm_fabsf(r)));
                pk[PART_EVENTS] = fm_maxf(pk[PART_EVENTS], ev_g * fm_maxf(fm_fabsf(el), fm_fabsf(er)));
                pk[PART_LINE] = fm_maxf(pk[PART_LINE], fm_maxf(fm_fabsf(ll), fm_fabsf(lr)));
                send += perc_g * riser(n, &l, &r) * 0.5f + atmos(w, n, &al, &ar) * a;
                al *= a;
                ar *= a;
                pk[PART_ATMOS] = fm_maxf(pk[PART_ATMOS], fm_maxf(fm_fabsf(al), fm_fabsf(ar)));
            }
            if (s.echo_d < 1.0f)
                s.echo_d = dt_e;
            {   /* the echo's kind, by degrees (~45 ms) */
                int ek = echo_kind;
                s.e_route += ((ek == PS_ECHO_STRAIGHT ? 1.0f : 0.0f) - s.e_route) * 0.0005f;
                s.e_smear += ((ek == PS_ECHO_SMEAR ? 1.0f : 0.0f) - s.e_smear) * 0.0005f;
                s.e_tape += ((ek == PS_ECHO_TAPE ? 1.0f : 0.0f) - s.e_tape) * 0.0005f;
            }
            if (s.e_tape > 0.0005f) {  /* TAPE: wow (0.6 Hz) and flutter (6.3 Hz), ~0.5 % and ~0.15 % */
                if ((s.e_wow += 0.6f / PS_SR) >= 1.0f)
                    s.e_wow -= 1.0f;
                if ((s.e_flut += 6.3f / PS_SR) >= 1.0f)
                    s.e_flut -= 1.0f;
                dt_e = fm_minf(dt_e * (1.0f + s.e_tape * (0.005f * fm_sin_turns(s.e_wow) + 0.0015f * fm_sin_turns(s.e_flut))),
                               (float)(ECHO_N - 2u));
            }
            if (dub_on()) {            /* DUB: half again as long over ~2 s, wavering ~0.9 % at 0.7 Hz */
                if ((s.dub_ph += 0.7f / PS_SR) >= 1.0f)
                    s.dub_ph -= 1.0f;
                dt_e = fm_minf(dt_e * 1.5f * (1.0f + 0.009f * fm_sin_turns(s.dub_ph)), (float)(ECHO_N - 2u));
                s.echo_d += (dt_e - s.echo_d) * 0.00002f;
            } else
                s.echo_d += (dt_e - s.echo_d) * 0.0002f;   /* ~0.1 s: a new time a short tape-like slide, no jump (DUB
                                                            * let go: springing back) */
            d = (uint32_t)s.echo_d;
            ef = s.echo_d - (float)d;
            {   /* between two samples */
                uint32_t a0 = (echo_w - d) % ECHO_N, a1 = (echo_w - d - 1u) % ECHO_N;
                xl = ((float)echo_buf[0][a0] + ((float)echo_buf[0][a1] - (float)echo_buf[0][a0]) * ef) * (1.0f / 32768.0f);
                xr = ((float)echo_buf[1][a0] + ((float)echo_buf[1][a1] - (float)echo_buf[1][a0]) * ef) * (1.0f / 32768.0f);
            }
            if (fb < 1.0f)
                send = hp2(&echo_hp, send + fx_send, HP150);   /* nothing under 150 Hz into the echo */
            else
                send = 0.0f;
            {   /* PING-PONG: into the left the right's and the send, into the right the left's; STRAIGHT: each its own,
                 * the send into both. TAPE darker, driven a little; SMEAR blurred */
                float rt = s.e_route, tn = tone * (1.0f - 0.35f * s.e_tape), wl, wr;
                s.e_lp[0] += ((xr + (xl - xr) * rt) * fb + send * (0.5f - 0.15f * rt) - s.e_lp[0]) * tn;
                s.e_lp[1] += ((xl + (xr - xl) * rt) * fb + send * 0.35f * rt - s.e_lp[1]) * tn;
                wl = s.e_lp[0];
                wr = s.e_lp[1];
                if (s.e_smear > 0.0005f) {
                    wl += (smear(0, wl) - wl) * s.e_smear;
                    wr += (smear(1, wr) - wr) * s.e_smear;
                }
                if (s.e_tape > 0.0005f) {
                    float g = 1.0f + 0.5f * s.e_tape;
                    wl = soft(wl * g) / g;
                    wr = soft(wr * g) / g;
                }
                if (fx.held >> FX_FREEZE & 1u) {   /* FREEZE: for one echo's time what is above the kick and the
                                                    * bass (the sample before's) taken in, both sides as they are,
                                                    * its ends ~2 ms slopes (the loop's seam: no click); then the echo
                                                    * as it is, again and again */
                    if (s.frz_t < s.echo_d) {
                        float win = fm_clampf(fm_minf(s.frz_t, s.echo_d - s.frz_t) / 88.0f, 0.0f, 1.0f);
                        wl = s.e_lp[0] = s.up[0] * win;
                        wr = s.e_lp[1] = s.up[1] * win;
                    } else {
                        wl = s.e_lp[0] = xl;
                        wr = s.e_lp[1] = xr;
                    }
                    s.frz_t += 1.0f;
                } else
                    s.frz_t = 0.0f;
                echo_buf[0][echo_w % ECHO_N] = (int16_t)(soft(wl) * 32767.0f);   /* (a throw rounded, not cut) */
                echo_buf[1][echo_w % ECHO_N] = (int16_t)(soft(wr) * 32767.0f);
            }
            echo_w++;
            ml = perc_g * l + ev_g * el + ll + al;     /* above the kick and the bass: WASH and DUB take it */
            mr = perc_g * r + ev_g * er + lr + ar;
            if (s.hp_hz > 10.0f) {     /* FILTER: the rest thinned from under too */
                float f = 6.2831853f * s.hp_hz / PS_SR, hl = ml - s.hp_u[0][0] - s.hp_u[0][1],
                      hr = mr - s.hp_u[1][0] - s.hp_u[1][1];
                s.hp_u[0][1] += f * hl;
                s.hp_u[0][0] += f * s.hp_u[0][1];
                s.hp_u[1][1] += f * hr;
                s.hp_u[1][0] += f * s.hp_u[1][1];
                ml += (hl - ml) * hp_g;
                mr += (hr - mr) * hp_g;
            } else
                s.hp_u[0][0] = s.hp_u[0][1] = s.hp_u[1][0] = s.hp_u[1][1] = 0.0f;
            s.lp_g += ((lp_on ? 1.0f : 0.0f) - s.lp_g) * 0.0005f;   /* ~45 ms */
            if (s.lp_g > 0.0005f) {    /* SINK: the rest closing from above (a Chamberlin's low, a little peak) */
                float f = cf.lp_f;
                s.lp_u[0][0] += f * s.lp_u[0][1];
                s.lp_u[0][1] += f * (ml - s.lp_u[0][0] - 0.7f * s.lp_u[0][1]);
                s.lp_u[1][0] += f * s.lp_u[1][1];
                s.lp_u[1][1] += f * (mr - s.lp_u[1][0] - 0.7f * s.lp_u[1][1]);
                ml += (s.lp_u[0][0] - ml) * s.lp_g;
                mr += (s.lp_u[1][0] - mr) * s.lp_g;
            } else
                s.lp_u[0][0] = s.lp_u[0][1] = s.lp_u[1][0] = s.lp_u[1][1] = 0.0f;
            s.up[0] = ml;              /* (FREEZE takes it in) */
            s.up[1] = mr;
            {   /* FREEZE: the return lifted (fx.c) */
                float rg = fm_maxf(ret_g, fx.ret_lift);
                outl[i] = c + ml + rg * xl;
                outr[i] = c + mr + rg * xr;
            }
            fx_send = fx_render(s.tick_len, &outl[i], &outr[i], ml, mr, n);
            {   /* the master: trimmed (the groove's peaks near 0.88), a limiter over 0.89 (at once down, back in
                 * ~90 ms, the two sides together), and a soft clip for what still gets past */
                float ol, orr, pk;
                s.gap_g += ((s.gap ? 0.0f : 1.0f) - s.gap_g) * 0.012f;   /* GAP: all of it out, ~2 ms slopes */
                ol = outl[i] * MASTER_TRIM * s.gap_g;
                orr = outr[i] * MASTER_TRIM * s.gap_g;
                pk = fm_maxf(fm_fabsf(ol), fm_fabsf(orr));
                if (pk * s.lim > 0.89f)
                    s.lim = 0.89f / pk;
                else
                    s.lim += (1.0f - s.lim) * 0.00025f;
                outl[i] = soft(ol * s.lim);
                outr[i] = soft(orr * s.lim);
            }
        }
    }
    {   /* the meters (the UI reads them): a peak held, falling back ~20 dB in 0.3 s */
        static float held[NPART];
        float fall = fm_expf(-(float)n / (0.13f * PS_SR));
        int k;
        for (k = 0; k < NPART; k++) {
            held[k] = fm_maxf(pk[k], held[k] * fall);
            if (held[k] < 0.0005f)
                held[k] = 0.0f;
            ps_ui.meter[k] = (uint8_t)(held[k] >= 1.0f ? 255.0f : held[k] * 255.0f);
        }
    }
}
