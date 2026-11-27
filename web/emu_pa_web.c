/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 RhinoTiger Pocket AMP port contributors */
/* The port in the browser: the host simulator (host/plat_host.c — the whole
 * literal app, UI and sound, against a simulated device) compiled to
 * WebAssembly, with the firmware's own Pocket AMP translator (pa_virt.c) as
 * the input surface. Mirrors upstream web/emu/emu_web.c for the FM-1; this
 * one drives the Pocket AMP controls and is a single translation unit.
 *
 * plat_host.c is the full host unity: it includes the literal gfx.c,
 * announce.c, touch.c and app.c itself. We add the PA translator and the
 * browser exports.
 *
 * Build (tools/build_web_pa.sh):
 *   emcc -O2 -ffp-contract=off -std=gnu99 -DPLAT_HOST -DPLAT_WEB \
 *        --no-entry -sSTANDALONE_WASM -sSTACK_SIZE=1048576 \
 *        -sINITIAL_MEMORY=33554432 -sFILESYSTEM=0 -o app.wasm web/emu_pa_web.c
 */
#include <stdint.h>

static void web_audio(const int32_t *blk, uint32_t n);   /* plat_host.c's run_ms calls it under PLAT_WEB */
#include "plat_host.c"          /* base/host: the simulated device + the app's platform + the literal app */

#include "pa_virt.c"            /* the Pocket AMP control translation (plat/pocketamp) */

static pa_virt_t pa_v;

/* the host's screen-reader wire (announce.c inside plat_host.c): no reader in
 * the browser, so nothing is sent */
static void web_said(const uint8_t *p, uint32_t n)
{
    (void)p; (void)n;
}

/* output ring, stereo float, filled by run_ms, drained by web_render */
#define RING 4096u
static float ring[RING * 2];
static uint32_t ring_w, ring_r;
static void web_audio(const int32_t *blk, uint32_t n)
{
    uint32_t k;
    for (k = 0; k < n; k++) {
        uint32_t i = (ring_w++ % RING) * 2u;
        ring[i] = (float)blk[2 * k] / 8388608.0f;
        ring[i + 1] = (float)blk[2 * k + 1] / 8388608.0f;
    }
}

static float out_l[1024], out_r[1024];
__attribute__((used, visibility("default"))) float *web_out_l(void) { return out_l; }
__attribute__((used, visibility("default"))) float *web_out_r(void) { return out_r; }

/* n frames (<= 1024) into out_l / out_r: the device runs until they exist */
__attribute__((used, visibility("default"))) void web_render(uint32_t n)
{
    uint32_t k;
    if (n > 1024u)
        n = 1024u;
    while (ring_w - ring_r < n)
        run_ms(1);
    for (k = 0; k < n; k++) {
        uint32_t i = (ring_r++ % RING) * 2u;
        out_l[k] = ring[i];
        out_r[k] = ring[i + 1];
    }
}

/* boot + the Pocket AMP panel: physical mask (PA_P_* bits), ms, PARA detent,
 * MASTER position 0..4096 -> the app's virtual buttons/keys/knobs */
__attribute__((used, visibility("default"))) void web_boot(void) { boot(); }
__attribute__((used, visibility("default"))) void web_pa(uint8_t d, uint32_t now, int enc_dir, uint32_t mstr)
{
    pa_virt_run(&pa_v, d, now, enc_dir, mstr);
    held_btn = pa_virt_buttons(&pa_v);
    held_keys = pa_virt_keys(&pa_v);
    {
        int i;
        for (i = 0; i < NE; i++)
            enc_acc[i] += pa_virt_take(&pa_v, i);
    }
    if (mstr <= 4096u)
        master = mstr;
}

__attribute__((used, visibility("default"))) uint32_t web_debug_ring_fill(void) { return ring_w - ring_r; }
__attribute__((used, visibility("default"))) float web_debug_ring_peak(void)
{
    float p = 0.0f;
    uint32_t i;
    for (i = 0; i < RING; i++) {
        float v = ring[2 * i], w = ring[2 * i + 1];
        if (v < 0) v = -v;
        if (w < 0) w = -w;
        if (v > p) p = v;
        if (w > p) p = w;
    }
    return p;
}
__attribute__((used, visibility("default"))) uint16_t *web_fb(void) { return fb; }
__attribute__((used, visibility("default"))) uint32_t web_blits(void) { return blits; }
__attribute__((used, visibility("default"))) uint32_t web_now(void) { return now_ms; }
__attribute__((used, visibility("default"))) uint32_t web_lit_buttons(void) { return lit_btn; }
__attribute__((used, visibility("default"))) uint32_t web_lit_keys(void) { return lit_keys; }
__attribute__((used, visibility("default"))) uint32_t web_dim_buttons(void) { return dim_btn; }
__attribute__((used, visibility("default"))) uint32_t web_dim_keys(void) { return dim_keys | mid_keys; }
