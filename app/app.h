/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 hacha */
/* RhinoTiger: the app's half of the contract with fm1-base (device/unity.c, host/plat_host.c). Its shape follows
 * fm1-base's template and PaperLagoon's app.h. */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "RHINOTIGER"
#define APP_SPLASH_RGB RGB(230, 150, 40)
#define APP_PALETTE 1u                   /* gfx.c PALETTES: amber */
#define APP_BLOCK 256u                   /* frames per audio block: 5.8 ms at 44.1 kHz */
#define APP_TOUCH                        /* velocity and pressure from the keys (lib/touch.h): an event's bite */

/* the store: the settings, and the panel calibration (the platform's) */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u}

/* the contract: the platform calls these */
void app_init(void);                     /* boot: the engine, then the saved settings (or defaults) */
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);   /* audio interrupt: n stereo frames, 24-bit */
void app_panic(void);                    /* silence now (before an update rewrites the flash) */
void app_ui_init(void);                  /* the first screen, after the splash */
void app_frame(void);                    /* the UI: input and a redraw, about 60 times a second */
void app_input_only(void);               /* between frames: input only */
void app_say(const char *a, const char *b);   /* a short message on the screen and to a screen reader */

/* the host simulator's checks: world, page, bar, playing, dirty, section, drops, lines, muted, fx, lights, bpm, events, thrown, paramN, engineN */
#define APP_HOST_EXPECT
int app_expect(const char *what, int *got);
