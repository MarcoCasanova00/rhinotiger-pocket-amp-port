/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 RhinoTiger Pocket AMP port contributors */
/* The virtual-panel translator, as a pure state machine: the physical panel
 * (PARA encoder + push, MASTER pot, HOME, VOL-, VOL+, <, >) becomes the
 * literal app's 14 B_* buttons, 7 EN_* knobs and 27 keys. It is compiled into
 * the firmware (input.c) and into the host emulator's helper (tools/pa_virt_cli.c),
 * so the emulator exercises exactly the code the device runs.
 *
 * Inputs, per call:
 *   d          the stable physical mask (PA_P_* bits; low = pressed)
 *   now        ms (monotonic), for the PARA click/hold sequences and the
 *              synthesized SAVE tap
 *   enc_dir    PARA encoder detent since the last call: -1 / 0 / +1
 *   master_pos the smoothed absolute MASTER position 0..4096 (the first call
 *              only establishes the baseline; the delta drives the second knob
 *              in WORLD and PARAM focuses)
 * Outputs via pa_virt_buttons / pa_virt_keys (held masks) and
 * pa_virt_take (accumulated detents per EN_* role, cleared). */
#pragma once
#include <stdint.h>

typedef struct {
    uint8_t focus;                      /* PA_F_* */
    int8_t keycur;                      /* 0..26 */
    uint8_t prev_d;                     /* the previous stable mask */
    uint8_t para_phase;                 /* 0 none, 1 <1 s, 2 GLO, 3 GLO+SAVE */
    uint8_t master_ready;
    uint32_t para_at, save_tap_until;
    int32_t master_prev, master_acc;
    int32_t encacc[NE];
    uint32_t btn, keys;                 /* the synthesized held masks */
} pa_virt_t;

void pa_virt_init(pa_virt_t *v);
void pa_virt_run(pa_virt_t *v, uint8_t d, uint32_t now, int enc_dir, uint32_t master_pos);
uint32_t pa_virt_buttons(const pa_virt_t *v);
uint32_t pa_virt_keys(const pa_virt_t *v);
int32_t pa_virt_take(pa_virt_t *v, int role);   /* detents since the last take */
