/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 RhinoTiger Pocket AMP port contributors */
/* The Pocket AMP's physical controls: six buttons (pa_input.h), the PARA
 * encoder on PC0/PC1, the MASTER pot on PA10 (ADC channel 2). The virtual-panel
 * translation itself is in pa_virt.c (shared with the host emulator); this
 * file only reads the hardware and feeds it in. New port code only. */
#include "platform.h"
#include "pa_virt.h"

/* MASTER ADC: JL_ADC (lsfr_base 0x10000 + map_adr(0x31, 0)); AD_CH_PA10 = 2. */
#define PA_ADC_CON (*(volatile uint32_t *)0x13100u)
#define PA_ADC_RES (*(volatile uint32_t *)0x13104u)

static pa_virt_t pa_v;
static volatile uint32_t pa_master_pos = 4096u;   /* 0..4096; full until the first read */
static uint32_t pa_master_raw, pa_master_last;
static int32_t pa_enc_pend;              /* PARA detents since the last run */

static void pa_sync(uint8_t d)
{
    uint32_t now = plat_ms();
    int e = pa_enc_pend;
    pa_enc_pend = 0;
    pa_virt_run(&pa_v, d, now, e, pa_master_pos);
}

/* The encoder: quadrature on PC0/PC1, both high at a detent; one detent per
 * step, no acceleration (the app's own knob behavior handles speed). */
void pa_enc_poll(void)
{
    static uint32_t prev = 3u;
    static const int8_t tab[16] = {0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0};
    uint32_t v = FM1_PR(FM1_PC, FM1_IN) & 3u;
    int dir = tab[(prev << 2) | v];
    prev = v;
    pa_enc_pend += dir;
}

/* ---- MASTER pot (main loop, ~100 Hz) ---- */
void pa_adc_init(void)
{
    /* PA10 as analog input: direction in, digital input off, no pulls. */
    FM1_PR(FM1_PA, FM1_DIR) |= 1u << 10;
    FM1_PR(FM1_PA, FM1_DIE) &= ~(1u << 10);
    FM1_PR(FM1_PA, FM1_PU) &= ~(1u << 10);
    FM1_PR(FM1_PA, FM1_PD) &= ~(1u << 10);
}

static uint32_t pa_adc_read(uint32_t ch)
{
    uint32_t c, n = 0;
    c = 0x6u | (0xFu << 12) | (1u << 3) | (1u << 5) | (1u << 6);
    c |= (ch & 0xFu) << 8;
    PA_ADC_CON = c;
    PA_ADC_CON |= 1u << 4;              /* en */
    PA_ADC_CON |= 1u << 6;              /* kickstart */
    while (!(PA_ADC_CON & (1u << 7)) && ++n < 100000u)
        ;
    c = PA_ADC_RES;
    PA_ADC_CON = 0;
    return c;
}

void pa_master_sample(void)
{
    uint32_t now = plat_ms(), s = 0, i;
    if (now - pa_master_last < 10u)
        return;
    pa_master_last = now;
    for (i = 0; i < 4u; i++)
        s += pa_adc_read(2u) & 0x3FFu;   /* AD_CH_PA10 */
    s >>= 2;
    pa_master_raw = (pa_master_raw * 3u + s) >> 2;
    pa_master_pos = pa_master_raw * 4u;
    if (pa_master_pos > 4096u)
        pa_master_pos = 4096u;
}

uint32_t plat_buttons(void)
{
    pa_sync(pa_in.down);
    return pa_virt_buttons(&pa_v);
}

uint32_t plat_keys(void)
{
    pa_sync(pa_in.down);
    return pa_virt_keys(&pa_v);
}

int32_t plat_enc(int role)
{
    pa_sync(pa_in.down);
    return pa_virt_take(&pa_v, role);
}

uint32_t plat_master(void)
{
    return pa_master_pos;
}

/* The 1 kHz TIMER5 tick: buttons and encoder every 5 ms. */
void panel_tick(void)
{
    static uint32_t tick;
    if (++tick % PA_POLL_TICKS)
        return;
    pa_input_poll();
    pa_enc_poll();
}

void fm1_timer5_irq(void)
{
    fm1_timer5_ack();
    panel_tick();
}
