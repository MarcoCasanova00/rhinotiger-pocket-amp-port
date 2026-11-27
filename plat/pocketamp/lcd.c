/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 RhinoTiger Pocket AMP port contributors */
/* The strip renderer's panel side: gfx.c draws byte-swapped RGB565 into .pool,
 * lcd_blit sends it to the ST7789-class panel over SPI1. The hardware pins and
 * the init table are RhinoTiger's own literal Pocket AMP BSP headers. */
#include <stdint.h>
#include "platform.h"
#include "pa_lcd_hw.h"
#include "pa_lcd_init.h"
#include "fm1_time.h"

static uint8_t lcd_small[8];
static uint16_t lcd_fillbuf[PA_LCD_WIDTH];
static uint8_t lcd_busy;                  /* a DMA transfer may still read its source */

void lcd_sync(void)
{
    if (!lcd_busy)
        return;
    pa_lcd_wait();
    pa_lcd_deselect();
    lcd_busy = 0;
}

static void lcd_cmd(uint8_t c)
{
    lcd_sync();
    pa_lcd_send_cmd(c);
    pa_lcd_wait();
    pa_lcd_deselect();
}

static void lcd_data(const void *p, uint32_t n)
{
    if (!n)
        return;
    lcd_sync();
    pa_lcd_send_data(p, n);
    lcd_busy = 1;                         /* lcd_sync completes it */
}

static void lcd_window(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    uint32_t x1 = x + w - 1u, y1 = y + h - 1u;
    lcd_small[0] = (uint8_t)(x >> 8);
    lcd_small[1] = (uint8_t)x;
    lcd_small[2] = (uint8_t)(x1 >> 8);
    lcd_small[3] = (uint8_t)x1;
    lcd_cmd(0x2A);
    lcd_data(lcd_small, 4);
    lcd_small[4] = (uint8_t)(y >> 8);     /* not touched by the x transfer's DMA */
    lcd_small[5] = (uint8_t)y;
    lcd_small[6] = (uint8_t)(y1 >> 8);
    lcd_small[7] = (uint8_t)y1;
    lcd_cmd(0x2B);
    lcd_data(lcd_small + 4, 4);
    lcd_cmd(0x2C);
}

void lcd_fill(uint32_t x, uint32_t y, uint32_t w, uint32_t h, uint16_t c)
{
    uint32_t i, n, k, sw = ((uint32_t)c >> 8) | ((uint32_t)c << 8);
    if (!w || !h || x >= PA_LCD_WIDTH || y >= PA_LCD_HEIGHT)
        return;
    if (x + w > PA_LCD_WIDTH)
        w = PA_LCD_WIDTH - x;
    if (y + h > PA_LCD_HEIGHT)
        h = PA_LCD_HEIGHT - y;
    lcd_sync();
    k = PA_LCD_WIDTH / w * w;             /* whole rows per DMA transfer */
    for (i = 0; i < k; i++)
        lcd_fillbuf[i] = (uint16_t)sw;
    lcd_window(x, y, w, h);
    for (n = w * h; n; n -= k) {
        if (k > n)
            k = n;
        lcd_data(lcd_fillbuf, k * 2u);
    }
}

void lcd_blit(uint32_t x, uint32_t y, uint32_t w, uint32_t h, const uint16_t *px)
{
    if (!w || !h)
        return;
    lcd_window(x, y, w, h);
    lcd_data(px, w * h * 2u);
}

static void lcd_seq(void)
{
    uint32_t r, i;
    for (r = 0; r < sizeof(PA_LCD_INIT) / sizeof(PA_LCD_INIT[0]); r++) {
        const pa_lcd_init_t *e = &PA_LCD_INIT[r];
        if (e->cmd == PA_LCD_INIT_DELAY) {
            fm1_delay_ms(e->len);
            continue;
        }
        lcd_cmd(e->cmd);
        if (e->len) {
            for (i = 0; i < e->len; i++)
                lcd_small[i] = e->data[i];
            lcd_data(lcd_small, e->len);
        }
    }
}

void lcd_init(void)
{
    pa_lcd_hw_init();
    fm1_delay_ms(120);
    lcd_seq();
    lcd_fill(0, 0, PA_LCD_WIDTH, PA_LCD_HEIGHT, 0);
}

void lcd_display_on(void)
{
    lcd_sync();
    lcd_cmd(0x29);
}
