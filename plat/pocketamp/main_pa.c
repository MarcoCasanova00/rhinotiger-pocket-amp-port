/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 RhinoTiger Pocket AMP port contributors */
/* Pocket AMP boot and main loop. The whole unity build (unity.c) is included
 * here: the literal app/renderer/announce/touch and the port's modules, one
 * translation unit. crt0.S calls fm1_cstart with the stacks set; the load
 * images (.ram_text, .data) are copied and .bss/.pool zeroed before any of the
 * code below runs. New port code only. */
#include "unity.c"

static void fm1_fault(const fm1_crash_t *c)
{
    (void)c;
    fm1_irq_off();
    lcd_fill(0, 0, PA_LCD_WIDTH, PA_LCD_HEIGHT, RGB(200, 40, 40));
    *(volatile uint32_t *)0x10000u |= 1u << 4;      /* core reset, as fault-in-fault */
    for (;;)
        ;
}

void fm1_cstart(void)
{
    extern char _rt_start[], _rt_end[], _rt_load[];
    extern char _data_start[], _data_end[], _data_load[];
    extern char _bss_start[], _bss_end[], _pool_start[], _pool_end[];
    uint32_t *w;
    uint32_t last_frame = 0;

    fm1_reset_reason();                 /* first thing: the reset's reasons */
    fm1_time_init();                    /* TIMER4 before any delay */

    /* the load images and the zeroed regions (the flash driver, the DSP and
     * the pool all assume them) */
    for (w = (uint32_t *)_rt_start; w < (uint32_t *)_rt_end; w++)
        *w = ((uint32_t *)_rt_load)[w - (uint32_t *)_rt_start];
    for (w = (uint32_t *)_data_start; w < (uint32_t *)_data_end; w++)
        *w = ((uint32_t *)_data_load)[w - (uint32_t *)_data_start];
    memset(_bss_start, 0, _bss_end - _bss_start);
    memset(_pool_start, 0, _pool_end - _pool_start);

    fm1_irq_init();                     /* fatal vectors, exception, ETM */
    fm1_clock_stock();                  /* the stock 360 MHz (RAM, IRQs off) */
    fm1_vdc14_raise(7);                 /* M-VAVE's V15 rails (1.60 V / 1.38 V) */
    fm1_sysvdd_raise(15);
    fm1_power_stock();
    fm1_wdt_arm(0xFu);                  /* reset watchdog, 32 s */
    fl_plain_window_init();             /* the store's window plain, unencrypted */
    fm1_cpu1_stop();                    /* hold the second core (never started) */

    /* the board */
    lcd_init();
    pa_input_init();
    pa_board_inputs();
    pa_adc_init();

    /* audio: buffers zeroed first, ALNK0 attached (prio 3, above the scan) */
    audio_silence();
    pa_audio_init(pa_dma_out[0], pa_dma_in[0], PA_HALF_WORDS, isr_alnk0, 3u);
    pa_render_block();                  /* prefill the ring (boot_hold silences) */
    pa_render_block();

    /* the app: the splash face, then the engine and its saved settings */
    app_splash();
    app_init();
    app_ui_init();
    lcd_display_on();

    /* the 1 kHz scan (prio 4) */
    fm1_timer5_start(isr_timer5, 4u);
    fm1_irq_enable_all();

    /* the main loop: ~60 frames/s, input in between */
    for (;;) {
        fm1_wdt_feed();
        pa_master_sample();
        audio_prof_tick();
        if (fm1_uptime_ms() - last_frame >= 16u) {
            last_frame = fm1_uptime_ms();
            app_frame();
        } else {
            app_input_only();
        }
    }
}
