/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 RhinoTiger Pocket AMP port contributors */
/* MIDI transport for the Pocket AMP: a 16-packet queue with RhinoTiger's
 * 4-byte USB-MIDI packet format (cin | status<<8 | data1<<16 | data2<<24) and
 * t24 stamps. This first port has no host: the queue stays empty and the out
 * side has no listeners (plat_midi_out_room 0), so the literal app's MIDI
 * behaviors — clock follow, note events, start/stop — are wired and ready for
 * a transport (USB/TRS) without changing the app. New port code only. */
#include "platform.h"

#define PA_MIDI_QN 16u
static uint32_t pa_midi_q[PA_MIDI_QN];
static uint32_t pa_midi_qn, pa_midi_qr;
static uint32_t pa_midi_last_t24;

void pa_midi_push(uint32_t pkt)
{
    if (pa_midi_qn < PA_MIDI_QN) {
        pa_midi_q[(pa_midi_qr + pa_midi_qn) & (PA_MIDI_QN - 1u)] = pkt;
        pa_midi_qn++;
    }
}

int plat_midi_in(uint32_t *pkt, int *port)
{
    if (!pa_midi_qn)
        return 0;
    *pkt = pa_midi_q[pa_midi_qr];
    pa_midi_qr = (pa_midi_qr + 1u) & (PA_MIDI_QN - 1u);
    pa_midi_qn--;
    if (port)
        *port = PORT_TRS;
    pa_midi_last_t24 = plat_ticks24();
    return 1;
}

uint32_t plat_midi_in_t24(void)
{
    return pa_midi_last_t24;
}

uint32_t plat_midi_in_from(void)
{
    return 0;
}

void plat_midi_send(uint32_t pkt, uint32_t to)
{
    (void)pkt;
    (void)to;
}

void plat_midi_out(uint32_t pkt)
{
    (void)pkt;
}

uint32_t plat_midi_out_room(uint32_t to)
{
    (void)to;
    return 0;                           /* no listener */
}

uint32_t plat_midi_out_link(void)
{
    return 0;
}

uint32_t plat_midi_count(int port, int out)
{
    (void)port;
    (void)out;
    return 0;
}

uint32_t plat_midi_ago(int port, int out)
{
    (void)port;
    (void)out;
    return 0xFFFFFFFFu;
}
