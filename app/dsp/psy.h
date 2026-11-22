/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 hacha */
/* RhinoTiger's engine: the kick and the rolling FM bass sharing the grid, the percussion over them, and the events.
 *
 * STATUS: a sketch. Eight worlds; the kick, the bass (a sixteenth roll, K-B-B-B), the percussion, the line, the events and
 * the atmos, with the parameters the pages turn (four to a page) and the main page's four macros; the sections (a break,
 * its build, the drop), cued or on their own.
 *
 * Threads: ps_render() runs in the audio interrupt. The UI writes the parameters (single halfwords) and posts the
 * keys to a queue the render drains as a block begins; what the render counts for the screen (ps_ui) only the UI
 * reads. Float only, no libm: fastmath.h. */
#pragma once
#include <stdint.h>

#define PS_SR 44100.0f

#define PS_NWORLD 8
const char *ps_world_name(int w);
void ps_init(void);
void ps_world(int w);                  /* from the next bar */
void ps_play(int on);
void ps_restart(void);                 /* from the top now (a MIDI start while playing) */
void ps_ext_bpm(float bpm);            /* a MIDI clock's tempo over the world's (0: none) */
int ps_playing(void);
int ps_bar(void);                      /* bars since PLAY (the screen, the host's checks) */
void ps_render(float *l, float *r, uint32_t n);   /* stereo, about -1..1 */

/* the parameters (the pages show them four at a time; app.c lays them out). New ones go at the end: a test's
 * "param N" keeps its meaning */
enum { P_ENERGY, P_DETAIL, P_DARK, P_DRIFT,                  /* MAIN: the macros */
       P_TEMPO, P_ROOT, P_FILLS, P_SITOUT,                   /* GROOVE: the world's tempo and root moved; fills; parts sitting out */
       P_KICK_LEVEL, P_KICK_TUNE, P_KICK_DRIVE, P_KICK_PUNCH, /* KICK */
       P_BASS_LEVEL, P_BASS_BITE, P_BASS_LEN, P_BASS_DRIVE,  /* BASS */
       P_PERC_LEVEL, P_HATS, P_HAT_TONE, P_CLAP,             /* PERC */
       P_EV_LEVEL, P_EV_DENSITY, P_EV_RUNS, P_EV_ECHO,       /* EVENTS */
       P_ECHO_LEVEL, P_ECHO_FB, P_ECHO_TONE, P_ECHO_TIME,    /* ECHO */
       P_ATMOS_LEVEL, P_ATMOS_COLOUR, P_ATMOS_AIR, P_ATMOS_WASH,   /* ATMOS */
       P_LINE_LEVEL, P_LINE_DENSITY, P_LINE_LEN, P_LINE_MUTATE,   /* LINE */
       P_BREAKS, P_BREAK_LEN, P_BUILD_LEN, P_GAP,            /* BREAK, BUILD: how often a break comes on its own (0: only
                                                              * cued), its bars, its build's, the bars after a drop */
       P_LINE_RANGE, P_LINE_SLIDE, P_LINE_BITE, P_LINE_DECAY,  /* LINE: how far its notes go, how many slide (its
                                                              * pattern page); its modulation's height, its fall */
       P_PERC_SEND, P_LINE_SEND,                             /* MIX: the percussion's, the line's into the echo (the
                                                              * events' is P_EV_ECHO, the atmos's P_ATMOS_WASH) */
       P_KICK_DECAY, P_BASS_DECAY, P_PERC_OPEN, P_LINE_DRIVE, /* the sounds' pages, where their levels were: the kick's
                                                              * body, how fast the bass's bite closes (its brightness
                                                              * once), the open hat's length, the line driven (0: clean) */
       P_EV_PITCH, P_EV_SPREAD, P_ATMOS_MOTION, P_ATMOS_DEPTH,   /* the events' semitones (-12..12) and how far apart
                                                              * left and right; the atmos's sway, how fast and how deep */
       P_MORPH,                                              /* BREAK's K4: how often the kit changes on its own (a
                                                              * drop's, a long groove's 32-bar block's) */
       P_NPARAMS };
typedef struct {
    const char *name;
    int16_t lo, hi, def;
} ps_param_t;
const ps_param_t *ps_param(int p);
void ps_set(int p, int v);
int ps_get(int p);
int ps_bpm(void);                      /* the tempo now (the world's, moved) */
int ps_root(void);                     /* the root now, MIDI note of the bass */
const char *ps_echo_time_name(int v);  /* P_ECHO_TIME's values */
const char *ps_phase_name(int p, int v);   /* P_BREAKS .. P_GAP's values as the screen says them */

/* the sections: the groove (MAIN), a break (the kick and the bass out, the atmos up), its build (BUILD's bars: a roll
 * and a rising noise), then the drop: everything back on a bar's first beat, a crash, and the bass a shade other than
 * before. A cue acts as the next bar begins: PS_CUE_BREAK (from the groove), PS_CUE_BUILD (from the groove or a break:
 * the build, then the drop), PS_CUE_DROP (the drop now). On their own, now and then (Drift and BREAKS), a break comes
 * as a 32-bar block begins, GAP bars after the last drop at least; it lasts BREAK bars (or the world's), its build
 * BUILD. */
enum { SEC_MAIN, SEC_BREAK, SEC_BUILD };
enum { PS_CUE_BREAK = 1, PS_CUE_DROP, PS_CUE_BUILD };
/* a build's kind (K1 on the BUILD page; AUTO: each build one of them, by the world's leaning, not the last
 * one again): ROLL (the kick and the bass out, a clap rolling quarters, eighths, sixteenths), KICK (the kick rolling
 * so, rising an octave; the bass out), FILTER (the kick on, the perc in from its second quarter, the bass from its
 * half; the rest and the bass thinned from under, the hats harder), TRIPLET (a clap rolling quarters, eighth and
 * sixteenth triplets, rising), STUTTER (the groove on, repeated: a quarter of it, an eighth, a sixteenth, a 32nd),
 * POLY (the kick on dotted eighths against the bar, closing to dotted sixteenths, then sixteenths; the clap on 2 and
 * 4), SINK (the other way: a clap rolling sixteenths, eighths, quarters, falling, the rest closing from above, the
 * riser falling), FAKE (a ROLL, then a drop for a beat, cut short: a bar of the clap's sixteenths, then the drop),
 * HALF (the groove in half time: the kick on 1 and 3, the clap on 3), ACID (the kick out, the bass alone on every
 * sixteenth, an octave up every third, its bite opening and screaming, the hats on the offbeats), STAIRS (the groove
 * on, the bass, the line, the atmos a minor third higher each quarter of it), GATE (the groove on, all of it cut:
 * quarters, eighths, sixteenths, 32nds, the first half of each heard). Each its riser. Taken as the build begins. */
enum { PS_BUILD_AUTO, PS_BUILD_ROLL, PS_BUILD_KICK, PS_BUILD_FILTER, PS_BUILD_TRIP, PS_BUILD_STUTTER, PS_BUILD_POLY,
       PS_BUILD_SINK, PS_BUILD_FAKE, PS_BUILD_HALF, PS_BUILD_ACID, PS_BUILD_STAIRS, PS_BUILD_GATE, PS_NBUILD };
void ps_build_kind(int k);
/* a build's ending (K2 on the BUILD page; AUTO: each build one of them, not the last one again), its
 * last bar (FAKE: before its false drop): BREATH (its last beat silent), RUSH (none: the roll twice as fast through its
 * last beat), VOID (its last bar silent but the riser, its last beat all silent; a 2-bar build: its last two beats),
 * TAPE (the music slowing to a stop over its last two beats), REVERSE (its last beat the one before, backwards), ECHO
 * (its third beat thrown into the echo, its last silent but the echo), PICKUP (its last beat silent but its last
 * sixteenth: the kick and the bass). Taken as the build begins. */
enum { PS_END_AUTO, PS_END_BREATH, PS_END_RUSH, PS_END_VOID, PS_END_TAPE, PS_END_REVERSE, PS_END_ECHO, PS_END_PICKUP,
       PS_NEND };
void ps_build_end(int k);
int ps_build_end_now(void);            /* the one chosen (AUTO or another) */
const char *ps_end_name(int k);
/* the bass's shape (ALGORITHM on GROOVE): AUTO (the world's: K-B-B-B, now and then a gallop, an offbeat, a bar of
 * triplets), or one held: K-B-B-B, GALLOP, OFFBEAT (from the next bar, no triplets) */
enum { PS_SHAPE_AUTO, PS_SHAPE_ROLL, PS_SHAPE_GALLOP, PS_SHAPE_OFF, PS_NSHAPE };
void ps_bass_shape(int k);
int ps_bass_shape_now(void);
const char *ps_bass_shape_name(int k);
/* the echo's kind (ALGORITHM on ECHO): PING-PONG (left, right, in turn), STRAIGHT (both at once, in the middle), TAPE
 * (wavering, darker, a little driven), SMEAR (each repeat blurred more, towards a reverb). Changed by degrees. */
enum { PS_ECHO_PING, PS_ECHO_STRAIGHT, PS_ECHO_TAPE, PS_ECHO_SMEAR, PS_NECHO };
void ps_echo_kind(int k);
int ps_echo_kind_now(void);
const char *ps_echo_kind_name(int k);
int ps_build_kind_now(void);           /* the one chosen (AUTO or another) */
const char *ps_build_name(int k);
void ps_cue(int how);
int ps_section(void);
void ps_shed(int on);                  /* lighter (the render near its time): the atmos one note, the events half their
                                        * voices, no FLANGE */
int ps_shedding(void);

/* the bass's colour (its type: PS_NTYPE at most), chosen now (most drops choose another on their own) */
#define PS_NCOLOUR PS_NTYPE
void ps_colour(int c);
int ps_colour_now(void);
/* each part's type (ALGORITHM on its page, or on MIX its track's): a few sounds of it (ps_ntypes, up to PS_NTYPE), 0
 * the world's own. A type takes from a part's next note. The bass's are its colours. */
#define PS_NTYPE 6
void ps_type(int part, int t);
int ps_type_now(int part);
int ps_ntypes(int part);
const char *ps_type_name(int part, int t);
void ps_line_new(void);                /* the line's phrase drawn anew as the next bar begins */
/* the kit (ALGORITHM on MAIN): turns forward, another type for one part or a few of those heard, as the next bar
 * begins (the turns of a bar together: more, more parts, up to three); the groove one of them (another shape for four
 * bars, GROOVE's staying); back, the kit before (up to eight). ps_ui.kit_parts: bit
 * PS_KIT_GROOVE the groove */
#define PS_KIT_GROOVE 6
void ps_kit(int turns);
void ps_octave(int o);                 /* the keys' events an octave or two up or down (-1..2) */

/* the performance (fx.c): the white keys' grid of sixteen effects, each while held (from the next sixteenth where it
 * takes the music's time), and the parts: muted (a black key tapped, again: back), out while a black key is held, and
 * a fill on the next beat */
#define PS_NFX 16
void ps_fx(int id, int on);
const char *ps_fx_name(int id);
enum { PART_KICK, PART_BASS, PART_PERC, PART_LINE, PART_EVENTS, PART_ATMOS, NPART };
void ps_mute(int part);                /* muted, or back */
void ps_out(int part, int on);         /* out while held */
void ps_fill(void);
void ps_half(int on);                  /* while held (1 .. 0): the kick on 1 and 3, the bass's roll in eighths */
void ps_gap(void);                     /* the last beat to come of a bar (this bar's, or the next's if it has begun)
                                        * silent, all of it */

/* the keys throw events (events.c): key 0..26 (F3..G5), down 1 (value: velocity), 0 up, 2 pressure (value 0..127). A
 * white key is a sound; a black key held, a rhythm for the white keys held with it */
void ps_key(int k, int down, int value);
int ps_stat(int which);                /* 0 events begun, 1 thrown and remembered (up to 8), 2 the latest thrown's type,
                                        * 3 the line's phrases drawn whole; its 32 steps as heard now: 4 on, 5 the
                                        * highest note (over the register), 6 sliding; 7 the parts out
                                        * while a black key is held (1 << PART_*); 8 GAP sounding (1), cued
                                        * (2), HALF held (4) */
const char *ps_event_name(int type);
int ps_rhythm(void);                   /* the black key held whose rhythm the white keys throw on (-1: none) */
void ps_rec(int on);                   /* REC: tapped (1), a take of the keys from the next bar, four bars at most (again
                                        * while armed: not; while taking: it ends with that bar), then it loops so long;
                                        * held (0), the loop gone */
int ps_take(void);                     /* the throws in the keys' loop (0: none) */
int ps_rec_state(void);                /* 0, 1 armed (the next bar), 2 taking */
int ps_rec_bar(int *of);               /* taking: its bar (1..), of its bars (4, or fewer once REC ends it) */
const char *ps_rhythm_name(int k);

/* for the screen: the bar now, lane by lane, as it will play (the parts are generated: this is the engine's plan for
 * the bar, not a record). out[i] for each step: 0 none, else 1..3 its weight (soft, middle, strong); for the line, 1 +
 * its note over the register + 2 (so a height). Returns the steps: 16 sixteenths, or 24 (the bass in triplets) */
enum { LANE_KICK, LANE_BASS, LANE_PERC, LANE_LINE, LANE_EVENTS, NLANE };
int ps_lane(int lane, uint8_t out[24]);
int ps_line_range(void);               /* the line's highest note over its register (the screen's scale) */

/* for the screen: what struck, counted (the UI sees a count move), and where the music is */
enum { HIT_KICK, HIT_BASS, HIT_PERC, HIT_EVENT, HIT_THROW, HIT_LINE, NHIT };
typedef struct {
    volatile uint32_t hits[NHIT];
    volatile uint32_t tick;            /* ticks since PLAY (24 a beat) */
    volatile int8_t thrown_type;       /* the latest thrown event's type (-1 none) */
    volatile int8_t sitting_out;       /* the part sitting out this phrase: 0 none, 1 hats, 2 open hat, 3 clap, 4 ride */
    volatile int8_t section;           /* SEC_* */
    volatile int8_t cued;              /* a cue waiting for the next bar: 0 none, else PS_CUE_* */
    volatile uint8_t sec_bar;          /* bars into the section (the groove: into its phrase, 0..7) */
    volatile uint8_t sec_len;          /* a build's bars (else 0) */
    volatile int8_t build_kind;        /* the build's kind, the latest's (PS_BUILD_*, 0 none yet) */
    volatile int8_t build_end;         /* its ending, the latest's (PS_END_*, 0 none yet) */
    volatile uint8_t drops;            /* drops so far (wraps) */
    volatile uint8_t muted;            /* the parts muted (1 << PART_*) */
    volatile uint8_t kits;             /* kits taken (wraps; back too) */
    volatile uint8_t kit_parts;        /* the parts the latest changed (1 << PART_*) */
    volatile uint8_t kit_depth;        /* kits to go back to */
    volatile int8_t kit_shape;         /* the kit's shape over GROOVE's now (PS_SHAPE_*; AUTO: none) */
    volatile uint8_t meter[NPART];     /* each part's peak as heard (its level; muted: none), held and falling back
                                        * ~20 dB in 0.3 s; 255 full scale */
} ps_ui_t;
extern ps_ui_t ps_ui;
#define PS_TPB 24u
