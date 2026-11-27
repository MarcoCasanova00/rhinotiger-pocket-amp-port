/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 RhinoTiger Pocket AMP port contributors */
/* Local configuration for the literal RhinoTiger app on the M-VAVE/Donner Pocket AMP.
 * This file is new port code; no upstream app/DSP source is changed. */
#pragma once

#define FM1_CPU1 0
#define FM1B_BLE 0
#define FM1B_BLE_CENTRAL 0
#define FM1B_WIFI 0
#define FM1B_CPU2 0
#define FM1B_UAC 0
#define FM1B_UAC_OUT 0
#define FM1B_TRS 0
#define FM1B_MIDI_STAMP 1
#define FM1B_INPUT_HZ 1000
#define FM1_T5_PERIOD 6000u              /* 6 MHz / 1000 Hz */

/* The flash part and the window the SFC maps as plain, unencrypted data.
 * The app image is far below 0x86000; these sectors are the port's store. */
#define FM1_FLASH_SIZE 0x100000u
#define FM1_FLASH_XIP_SIZE 0x100000u
#define FM1_FLASH_PLAIN_LO 0x86000u
#define FM1_FLASH_PLAIN_HI 0xB4000u
#define FM1_FLASH_WRITE_REGIONS(X) X(0x86000u, 0xB4000u)

#define PA_LCD_BAUD 1u
#define PA_LCD_WIDTH 240u
#define PA_LCD_HEIGHT 240u

/* Pocket AMP ALNK0: 64-frame halves, full duplex, 44.1 kHz, 24-bit right-aligned. */
#define PA_FS 44100u
#define PA_HALF_FRAMES 64u
#define PA_HALF_WORDS (PA_HALF_FRAMES * 2u)
#define PA_RING_HALVES 8u                 /* two APP_BLOCKs ahead of the DMA */
#define PA_RING_WORDS (PA_RING_HALVES * PA_HALF_WORDS)

/* Input scan: the 1 kHz TIMER5 tick polls the buttons and the encoder every
 * 5 ms; pa_input.h's PA_DEB_TICKS 3 then debounces to 15 ms. */
#define PA_POLL_TICKS 5u

/* The persistent store: two A/B 4 KiB sector pairs in the writable window
 * (safe APP_STORE_MAP for this board). A sector holds a 32-byte header at its
 * start, written last, and the payload at offset 256; the highest seq wins. */
#define PA_STORE_BASE 0x86000u
#define PA_STORE_SECTOR 4096u
#define PA_STORE_PAYLOAD_OFF 256u
#define PA_STORE_HEADER 32u
#define PA_STORE_MAGIC 0x534B4150u        /* "PAKS" */
#define PA_STORE_NOBJ 2u

/* Physical panel bit numbers, from pa_input.h. */
enum {
    PA_P_PARA = 0, PA_P_HOME = 1, PA_P_VOLD = 2, PA_P_VOLU = 3,
    PA_P_LEFT = 4, PA_P_RIGHT = 5, PA_P_N
};

/* The two knobs' focus, cycled by a PARA tap. */
enum { PA_F_PAGE, PA_F_WORLD, PA_F_K1, PA_F_K2, PA_F_K3, PA_F_K4, PA_F_ALGO, PA_F_KEY, PA_NFOCUS };

/* The assembly IRQ wrappers (fm1_isr.S) call these C bodies. */
void fm1_timer5_irq(void);
void fm1_alnk0_irq(void);
/* The assembly labels passed to the timer and ALNK0 attach calls. */
extern void isr_timer5(void);
extern void isr_alnk0(void);

void pa_fault_report(void);              /* main_pa.c: fatal handler wiring */
void lcd_init(void);                     /* lcd.c: panel setup */
void lcd_display_on(void);               /* lcd.c: 0x29, after the first frame is in RAM */
void audio_silence(void);                /* audio.c: zero the ring and the DMA buffers */
void audio_prof_tick(void);              /* audio.c: main loop; render-load % and app_overload */
void panel_tick(void);                   /* input.c: called from the 1 kHz timer ISR */
void pa_master_sample(void);             /* input.c: MASTER pot, called from the main loop */
void pa_adc_init(void);                  /* input.c */
void pa_midi_push(uint32_t pkt);         /* midi.c: a transport queues a 4-byte packet */
