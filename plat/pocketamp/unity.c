/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 RhinoTiger Pocket AMP port contributors */
/* The Pocket AMP unity translation unit: the literal RhinoTiger app, renderer,
 * announce logic and touch logic, in the same order as fm1-base's device/unity.c,
 * with the port's platform modules. Everything except the assembly startup
 * (crt0.S, fm1_vec.S, fm1_isr.S) and the -O2 DSP unit (app/dsp/psy.c) compiles
 * here, so the literal files' static state stays inside one translation unit.
 *
 * No upstream app/DSP file is edited; this file only includes them. */
#include <stdint.h>

#include "platform.h"
#include "plat.h"
#include "app.h"
#include "touch.h"

/* BSP register-level headers (literal; the port adds no SDK driver source). */
#include "fm1_time.h"
#include "fm1_sys.h"
#include "fm1_irq.h"
#include "fm1_gpio.h"
#include "fm1_timer.h"
#include "fm1_flash.h"
#include "fm1_xip.h"
#include "fm1_clock.h"

/* The literal Pocket AMP board headers. */
#include "pa_input.h"
#include "pa_audio.h"
#include "pa_lcd_hw.h"
#include "pa_lcd_init.h"

/* The literal base device libraries, in fm1-base's order: libc first, then the
 * renderer's panel side (lcd.c) so gfx.c sees its non-static hooks, then gfx.c,
 * then the configuration defaults, the screen-reader words and announce.c. */
#include "libc.c"
#include "lcd.c"
#include "gfx.c"
#include "fm1b_config.h"
#include "ann_words.h"

static int ann_wire_send(const uint8_t *p, uint32_t n)
{
    (void)p;
    (void)n;
    return 0;                       /* no screen reader on the Pocket AMP wire */
}
#include "announce.c"

/* The port's platform modules. */
#include "pa_virt.c"
#include "input.c"
#include "audio.c"
#include "midi.c"
#include "store.c"
#include "plat.c"

/* The literal touch and app sources last, as in fm1-base's unity (touch before
 * app: the app uses its state API). */
#include "touch.c"
#include "app.c"
