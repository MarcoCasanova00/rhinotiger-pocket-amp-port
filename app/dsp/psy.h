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

/* the parameters, four to a page */
enum { P_ENERGY, P_DETAIL, P_DARK, P_DRIFT,                  /* MAIN: the macros */
       P_TEMPO, P_ROOT, P_FILLS, P_SITOUT,                   /* GROOVE: the world's tempo and root moved; fills; parts sitting out */
       P_KICK_LEVEL, P_KICK_TUNE, P_KICK_DRIVE, P_KICK_PUNCH, /* KICK */
       P_BASS_LEVEL, P_BASS_BITE, P_BASS_LEN, P_BASS_DRIVE,  /* BASS */
       P_PERC_LEVEL, P_HATS, P_HAT_TONE, P_CLAP,             /* PERC */
       P_EV_LEVEL, P_EV_DENSITY, P_EV_RUNS, P_EV_ECHO,       /* EVENTS */
       P_ECHO_LEVEL, P_ECHO_FB, P_ECHO_TONE, P_ECHO_TIME,    /* ECHO */
       P_ATMOS_LEVEL, P_ATMOS_COLOUR, P_ATMOS_AIR, P_ATMOS_WASH,   /* ATMOS */
       P_LINE_LEVEL, P_LINE_DENSITY, P_LINE_LEN, P_LINE_MUTATE,   /* LINE */
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

/* the sections: the groove (MAIN), a break (the kick and the bass out, the atmos up), its build (4 bars: a roll and a
 * rising noise), then the drop: everything back on a bar's first beat, a crash, and the bass a shade other than before.
 * A cue acts as the next bar begins: 1 (SAVE tapped) the next section (the groove: a break; a break: its build),
 * 2 (SAVE held) the drop now. On their own, now and then (Drift), a break comes as a 32-bar block begins. */
enum { SEC_MAIN, SEC_BREAK, SEC_BUILD };
void ps_cue(int how);
int ps_section(void);
void ps_auto(int on);
void ps_shed(int on);                  /* lighter (the render near its time): the atmos one note, the events half their
                                        * voices, no FLANGE */
int ps_shedding(void);                  /* breaks on their own (1, the default) or only when cued (0) */

/* the bass's colour (one of four: its ratio and bite), chosen now (each drop chooses another on its own) */
#define PS_NCOLOUR 4
void ps_colour(int c);
int ps_colour_now(void);
/* each part's type (ALGORITHM on its page): four sounds of it, 0 the world's own. A type takes from a part's next
 * note. The bass's are its colours. A part without types yet has one (ps_ntypes 1). */
#define PS_NTYPE 4
void ps_type(int part, int t);
int ps_type_now(int part);
int ps_ntypes(int part);
const char *ps_type_name(int part, int t);
void ps_line_new(void);                /* the line's phrase drawn anew as the next bar begins */
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

/* the keys throw events (events.c): key 0..26 (F3..G5), down 1 (value: velocity), 0 up, 2 pressure (value 0..127). A
 * white key is a sound; a black key held, a rhythm for the white keys held with it */
void ps_key(int k, int down, int value);
int ps_stat(int which);                /* 0 events begun, 1 thrown and remembered (up to 8), 2 the latest thrown's type */
const char *ps_event_name(int type);
int ps_rhythm(void);                   /* the black key held whose rhythm the white keys throw on (-1: none) */
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
    volatile int8_t cued;              /* a cue waiting for the next bar: 0 none, 1 next, 2 drop */
    volatile uint8_t sec_bar;          /* bars into the section (the groove: into its phrase, 0..7) */
    volatile uint8_t drops;            /* drops so far (wraps) */
    volatile uint8_t muted;            /* the parts muted (1 << PART_*) */
} ps_ui_t;
extern ps_ui_t ps_ui;
#define PS_TPB 24u
