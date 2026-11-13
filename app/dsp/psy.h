/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 hacha */
/* RhinoTiger's engine: the kick and the rolling FM bass sharing the grid.
 *
 * STATUS: the first sketch. The kick and the bass only: a sixteenth roll (K-B-B-B) and, to compare, a triplet one;
 * the bass rests on the last beat of every 8th bar, and the kick too on that of every 32nd (the fills).
 *
 * Threads: ps_render() runs in the audio interrupt; ps_world() and ps_play() only set a value it reads at a block's
 * start. Float only, no libm: fastmath.h. */
#pragma once
#include <stdint.h>

#define PS_SR 44100.0f

#define PS_NWORLD 3
const char *ps_world_name(int w);
void ps_init(void);
void ps_world(int w);                  /* from the next bar */
void ps_play(int on);
int ps_playing(void);
int ps_bar(void);                      /* bars since PLAY (the screen, the host's checks) */
void ps_render(float *out, uint32_t n);   /* mono, about -1..1 */
