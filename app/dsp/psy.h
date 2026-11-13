/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 hacha */
/* RhinoTiger's engine: the kick and the rolling FM bass sharing the grid, the percussion over them, and the events.
 *
 * STATUS: a sketch. Two worlds; the kick, the bass (a sixteenth roll, K-B-B-B), the percussion and the events, with
 * the parameters the pages turn (four to a page) and the main page's four macros.
 *
 * Threads: ps_render() runs in the audio interrupt. The UI writes the parameters (single halfwords) and posts the
 * keys to a queue the render drains as a block begins; what the render counts for the screen (ps_ui) only the UI
 * reads. Float only, no libm: fastmath.h. */
#pragma once
#include <stdint.h>

#define PS_SR 44100.0f

#define PS_NWORLD 2
const char *ps_world_name(int w);
void ps_init(void);
void ps_world(int w);                  /* from the next bar */
void ps_play(int on);
int ps_playing(void);
int ps_bar(void);                      /* bars since PLAY (the screen, the host's checks) */
void ps_render(float *l, float *r, uint32_t n);   /* stereo, about -1..1 */

/* the parameters, four to a page */
enum { P_ENERGY, P_DETAIL, P_DARK, P_DRIFT,                  /* MAIN: the macros */
       P_TEMPO, P_ROOT, P_FILLS, P_SITOUT,                   /* GROOVE: the world's tempo and root moved; fills; parts sitting out */
       P_KICK_LEVEL, P_KICK_TUNE, P_KICK_LEN, P_KICK_PUNCH,  /* KICK */
       P_BASS_LEVEL, P_BASS_BITE, P_BASS_LEN, P_BASS_DRIVE,  /* BASS */
       P_PERC_LEVEL, P_HATS, P_HAT_TONE, P_CLAP,             /* PERC */
       P_EV_LEVEL, P_EV_DENSITY, P_EV_RUNS, P_EV_ECHO,       /* EVENTS */
       P_ECHO_LEVEL, P_ECHO_FB, P_ECHO_TONE, P_ECHO_TIME,    /* ECHO */
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

/* the keys throw events (events.c): key 0..26 (F3..G5), down 1 (value: velocity), 0 up, 2 pressure (value 0..127) */
void ps_key(int k, int down, int value);
int ps_stat(int which);                /* 0 events begun, 1 thrown and remembered (up to 8), 2 the latest thrown's type */
const char *ps_event_name(int type);

/* for the screen: what struck, counted (the UI sees a count move), and where the music is */
enum { HIT_KICK, HIT_BASS, HIT_PERC, HIT_EVENT, HIT_THROW, NHIT };
typedef struct {
    volatile uint32_t hits[NHIT];
    volatile uint32_t tick;            /* ticks since PLAY (24 a beat) */
    volatile int8_t thrown_type;       /* the latest thrown event's type (-1 none) */
    volatile int8_t sitting_out;       /* the part sitting out this phrase: 0 none, 1 hats, 2 open hat, 3 clap, 4 ride */
} ps_ui_t;
extern ps_ui_t ps_ui;
#define PS_TPB 24u
