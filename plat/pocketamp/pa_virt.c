/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 RhinoTiger Pocket AMP port contributors */
#include <stdint.h>
#include "platform.h"
#include "plat.h"
#include "pa_virt.h"

static int pa_focus_role(const pa_virt_t *v)
{
    switch (v->focus) {
    case PA_F_PAGE:
        return EN_SELECT;
    case PA_F_WORLD:
        return EN_PRESET;
    case PA_F_ALGO:
        return EN_ALGO;
    default:
        return v->focus >= PA_F_K1 && v->focus <= PA_F_K4
                   ? EN_K1 + (v->focus - PA_F_K1)
                   : -1;
    }
}

static int pa_master_role(const pa_virt_t *v)
{
    if (v->focus == PA_F_WORLD)
        return EN_PRESET;
    if (v->focus >= PA_F_K1 && v->focus <= PA_F_K4)
        return EN_K1 + (v->focus - PA_F_K1);
    return -1;
}

void pa_virt_init(pa_virt_t *v)
{
    v->focus = PA_F_PAGE;
    v->keycur = 0;
    v->prev_d = 0;
    v->para_phase = 0;
    v->master_ready = 0;
    v->para_at = 0;
    v->save_tap_until = 0;
    v->master_prev = 0;
    v->master_acc = 0;
    {
        int i;
        for (i = 0; i < NE; i++)
            v->encacc[i] = 0;
    }
}

void pa_virt_run(pa_virt_t *v, uint8_t d, uint32_t now, int enc_dir, uint32_t master_pos)
{
    uint32_t b = 0, k = 0;
    int n;

    /* API encoder: KEY focus moves the cursor; else the focus's knob. */
    if (enc_dir) {
        if (v->focus == PA_F_KEY)
            v->keycur = (int8_t)clamp(v->keycur + enc_dir, 0, NKEYS - 1);
        else {
            int r = pa_focus_role(v);
            if (r >= 0)
                v->encacc[r] += enc_dir;
        }
    }

    /* MASTER delta on WORLD and PARAM focuses; the first call's baseline. */
    if (!v->master_ready) {
        v->master_ready = 1;
        v->master_prev = (int32_t)master_pos;
    } else {
        int32_t dlt = (int32_t)master_pos - v->master_prev;
        v->master_prev = (int32_t)master_pos;
        v->master_acc += dlt;
        while (v->master_acc >= 128) {
            int r = pa_master_role(v);
            v->master_acc -= 128;
            if (r >= 0)
                v->encacc[r]++;
        }
        while (v->master_acc <= -128) {
            int r = pa_master_role(v);
            v->master_acc += 128;
            if (r >= 0)
                v->encacc[r]--;
        }
    }

    /* PARA click/hold sequencing. */
    if (d & (1u << PA_P_PARA)) {
        if (!v->para_at) {
            v->para_at = now;
            v->para_phase = 1;
        }
        if (v->para_phase == 1 && now - v->para_at >= 1000u)
            v->para_phase = 2;
        if (v->para_phase == 2 && now - v->para_at >= 3000u)
            v->para_phase = 3;
    } else if (v->para_at) {
        uint32_t held = now - v->para_at;
        if (held < 1000u) {             /* a click: the focus cycles */
            v->focus = (uint8_t)((v->focus + 1) % PA_NFOCUS);
            v->master_prev = (int32_t)master_pos;
            v->master_acc = 0;
            v->prev_d = d;              /* no cursor edge from keys held before */
        } else if (held < 3000u) {
            v->save_tap_until = now + 30u;
        }
        v->para_at = 0;
        v->para_phase = 0;
    }
    if (v->para_phase >= 2)
        b |= 1u << B_GLO;
    if (v->para_phase >= 3)
        b |= 1u << B_SAVE;
    if ((int32_t)(now - v->save_tap_until) < 0)
        b |= 1u << B_SAVE;

    /* HOME is the system modifier in every focus. */
    if (d & (1u << PA_P_HOME)) {
        if (d & (1u << PA_P_VOLD))
            b |= 1u << B_GLO;
        if (d & (1u << PA_P_VOLU))
            b |= 1u << B_FX;
        if (d & (1u << PA_P_LEFT))
            b |= 1u << B_OCTDN;
        if (d & (1u << PA_P_RIGHT))
            b |= 1u << B_OCTUP;
    } else if (v->focus == PA_F_KEY) {
        if (d & (1u << PA_P_VOLD))
            k |= 1u << v->keycur;
        if (d & (1u << PA_P_VOLU))
            k |= 1u << v->keycur;
        if ((d & (1u << PA_P_LEFT)) && !(v->prev_d & (1u << PA_P_LEFT)))
            v->keycur = (int8_t)clamp(v->keycur - 1, 0, NKEYS - 1);
        if ((d & (1u << PA_P_RIGHT)) && !(v->prev_d & (1u << PA_P_RIGHT)))
            v->keycur = (int8_t)clamp(v->keycur + 1, 0, NKEYS - 1);
    } else {
        uint8_t q = d & 0x3Cu;          /* the four musical keys' physical bits */
        n = (q >> 2 & 1u) + (q >> 3 & 1u) + (q >> 4 & 1u) + (q >> 5 & 1u);
        if (n == 2) {
            switch (q) {
            case (1u << PA_P_VOLD) | (1u << PA_P_VOLU):
                b |= 1u << B_HOME;
                break;
            case (1u << PA_P_LEFT) | (1u << PA_P_RIGHT):
                b |= 1u << B_SEQ;
                break;
            case (1u << PA_P_VOLD) | (1u << PA_P_LEFT):
                b |= 1u << B_SEL;
                break;
            case (1u << PA_P_VOLU) | (1u << PA_P_RIGHT):
                b |= 1u << B_ENV;
                break;
            case (1u << PA_P_VOLD) | (1u << PA_P_RIGHT):
                b |= 1u << B_LFO;
                break;
            case (1u << PA_P_VOLU) | (1u << PA_P_LEFT):
                b |= 1u << B_EDIT;
                break;
            }
        } else if (n > 2) {
            b |= 1u << B_HOME;
        } else if (n == 1) {
            if (q & (1u << PA_P_VOLD))
                b |= 1u << B_PLAY;
            if (q & (1u << PA_P_VOLU))
                b |= 1u << B_ARP;
            if (q & (1u << PA_P_LEFT))
                b |= 1u << B_SAVE;
            if (q & (1u << PA_P_RIGHT))
                b |= 1u << B_REC;
        }
    }
    v->btn = b;
    v->keys = k;
    v->prev_d = d;
}

uint32_t pa_virt_buttons(const pa_virt_t *v)
{
    return v->btn;
}

uint32_t pa_virt_keys(const pa_virt_t *v)
{
    return v->keys;
}

int32_t pa_virt_take(pa_virt_t *v, int role)
{
    int32_t e;
    if (role < 0 || role >= NE)
        return 0;
    e = v->encacc[role];
    v->encacc[role] = 0;
    return e;
}
