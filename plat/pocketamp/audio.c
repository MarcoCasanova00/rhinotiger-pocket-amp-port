/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 RhinoTiger Pocket AMP port contributors */
/* Pocket AMP audio: ALNK0 I2S (pa_audio.h), two 64-frame double buffers, full
 * duplex. The app renders 256-frame blocks (APP_BLOCK) into a ring that feeds
 * the DMA halves; the input halves are acknowledged and discarded (RhinoTiger
 * has no audio input path). The render runs in the ALNK0 interrupt, as on the
 * FM-1. New port code only. */
#include "platform.h"

/* app.h does not prototype app_overload (the platform decides when to shed). */
extern void app_overload(void);

#define PA_RING_MASK (PA_RING_WORDS - 1u)

/* The DMA's double buffers and the render ring. Zeroed before pa_audio_init
 * (audio_silence), as pa_audio.h demands. */
static int32_t pa_dma_out[2][PA_HALF_WORDS] __attribute__((aligned(4)));
static int32_t pa_dma_in[2][PA_HALF_WORDS] __attribute__((aligned(4)));
static int32_t pa_ring[PA_RING_WORDS] __attribute__((aligned(4)));
static uint32_t pa_rd, pa_wr;
static int32_t pa_lr[APP_BLOCK * 2] __attribute__((aligned(4)));

static volatile uint32_t pa_xruns;
static uint32_t pa_render_us, pa_window_t0, pa_cpu_pct;

void audio_silence(void)
{
    memset(pa_dma_out, 0, sizeof pa_dma_out);
    memset(pa_dma_in, 0, sizeof pa_dma_in);
    memset(pa_ring, 0, sizeof pa_ring);
    pa_rd = pa_wr = 0;
}

static void pa_render_block(void)
{
    uint32_t t0 = fm1_ticks(), i;
    app_render(pa_lr, APP_BLOCK, plat_master());
    pa_render_us += (fm1_ticks() - t0) / 24u;
    for (i = 0; i < APP_BLOCK * 2u; i++)
        pa_ring[(pa_wr + i) & PA_RING_MASK] = pa_lr[i];
    pa_wr = (pa_wr + APP_BLOCK * 2u) & PA_RING_MASK;
}

void fm1_alnk0_irq(void)
{
    uint8_t p = pa_audio_pending();
    if (p & PA_AUDIO_IN) {
        pa_audio_ack_in();              /* input unused: acknowledged, discarded */
    }
    if (p & PA_AUDIO_OUT) {
        uint32_t h = pa_audio_free_half(), len = (pa_wr - pa_rd) & PA_RING_MASK;
        int32_t *dst = pa_dma_out[h];
        uint32_t i;
        if (len >= PA_HALF_WORDS) {
            for (i = 0; i < PA_HALF_WORDS; i++)
                dst[i] = pa_ring[(pa_rd + i) & PA_RING_MASK];
            pa_rd = (pa_rd + PA_HALF_WORDS) & PA_RING_MASK;
        } else {
            for (i = 0; i < PA_HALF_WORDS; i++)
                dst[i] = 0;
            pa_xruns++;
        }
        pa_audio_ack_out();
        if (((pa_wr - pa_rd) & PA_RING_MASK) <= PA_RING_WORDS / 2u)
            pa_render_block();
    }
}

/* Main loop: the render's share of the last half second; over 90 % asks the
 * app to shed (app_overload). */
void audio_prof_tick(void)
{
    uint32_t now = plat_ms(), dt = now - pa_window_t0;
    if (dt < 500u)
        return;
    pa_cpu_pct = pa_render_us * 100u / dt;
    pa_render_us = 0;
    pa_window_t0 = now;
    if (pa_cpu_pct > 90u)
        app_overload();
}

uint32_t plat_cpu_pct(void)
{
    return pa_cpu_pct;
}

uint32_t plat_xruns(void)
{
    return pa_xruns;
}
