/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 RhinoTiger Pocket AMP port contributors */
/* The plat.h contract the literal app uses, as safe "not available" returns on
 * the Pocket AMP port: no USB, Bluetooth, WiFi, second core, disk mode, console
 * or SysEx. The audio, input, store and MIDI sides are in audio.c, input.c,
 * store.c and midi.c. New port code only. */
#include "platform.h"

/* The keys: the Pocket AMP has no analog sensing (plat_sense off, no analog
 * rows); touch.c's touch_keys() returns our virtual mask directly. */
void plat_sense(int on)
{
    (void)on;
}

uint32_t plat_analog_mask(void)
{
    return 0;
}

/* The unique ID is unknown on this board (0 = unknown). */
const uint8_t *plat_chip_uid(void)
{
    static const uint8_t uid[16] = {0};
    return uid;
}

uint32_t plat_ms(void)
{
    return fm1_uptime_ms();
}

int plat_battery(void)
{
    return -1;
}

int32_t plat_battery_raw(void)
{
    return 0;
}

int plat_headphones(void)
{
    return -1;
}

void plat_lights(uint32_t buttons, uint32_t keys, uint32_t dim_buttons, uint32_t dim_keys, int glow)
{
    (void)buttons;
    (void)keys;
    (void)dim_buttons;
    (void)dim_keys;
    (void)glow;
}

void plat_lights_mid(uint32_t buttons, uint32_t keys)
{
    (void)buttons;
    (void)keys;
}

void plat_dim_ns(uint32_t ns)
{
    (void)ns;
}

void plat_leds(uint32_t buttons, uint32_t keys)
{
    (void)buttons;
    (void)keys;
}

void plat_glow(int on)
{
    (void)on;
}

void plat_play_red(int red)
{
    (void)red;
}

/* USB host: never (the port has no USB stack). */
void plat_usb_host(int on)
{
    (void)on;
}

int plat_usb_host_state(void)
{
    return USBH_OFF;
}

int plat_usb_host_msg(void)
{
    return USBH_MSG_NONE;
}

int plat_usb_lost(void)
{
    return 0;
}

const char *plat_usb_host_name(void)
{
    return "";
}

/* Bluetooth MIDI: off (FM1B_BLE 0). */
int plat_ble_state(void)
{
    return BLE_OFF;
}

int plat_ble_connected(void)
{
    return 0;
}

int plat_ble_linked(void)
{
    return 0;
}

void plat_ble_start(void)
{
}

int plat_ble_stream(const struct plat_ble_stream *s)
{
    (void)s;
    return 0;
}

void plat_ble_link_info(uint32_t v[7])
{
    uint32_t i;
    for (i = 0; i < 7u; i++)
        v[i] = 0;
}

uint32_t plat_ble_log_total(void)
{
    return 0;
}

const char *plat_ble_log_line(int back)
{
    (void)back;
    return 0;
}

void plat_ble_scan(int on)
{
    (void)on;
}

int plat_ble_found(int i, char *name, int *rssi)
{
    (void)i;
    (void)name;
    (void)rssi;
    return 0;
}

int plat_ble_connect(int i)
{
    (void)i;
    return 0;
}

void plat_ble_disconnect(void)
{
}

int plat_ble_device(void)
{
    return BLE_DEV_OFF;
}

int plat_ble_passkey(void)
{
    return 0;
}

int plat_ble_connect_peer(const struct plat_ble_peer *p)
{
    (void)p;
    return -1;
}

int plat_ble_device_fail(uint32_t *code)
{
    (void)code;
    return 0;
}

uint32_t plat_ble_device_flags(void)
{
    return 0;
}

int plat_ble_rpa_resolve(const uint8_t irk[16], const uint8_t addr[6])
{
    (void)irk;
    (void)addr;
    return 0;
}

void plat_ble_enable(int on)
{
    (void)on;
}

int app_ble_load(struct plat_ble_rec *r)
{
    (void)r;
    return -1;
}

int app_ble_save(const struct plat_ble_rec *r)
{
    (void)r;
    return -1;
}

/* Bluetooth audio: off. */
void plat_bta_on(int on)
{
    (void)on;
}

void plat_bta_scan(int on)
{
    (void)on;
}

int plat_bta_found(int i, char *name)
{
    (void)i;
    (void)name;
    return 0;
}

int plat_bta_known(int i, char *name)
{
    (void)i;
    (void)name;
    return 0;
}

void plat_bta_pick(int i)
{
    (void)i;
}

int plat_bta_reconnect(int i)
{
    (void)i;
    return 0;
}

void plat_bta_forget(int i)
{
    (void)i;
}

void plat_bta_hangup(void)
{
}

int plat_bta_state(void)
{
    return PLAT_BTA_OFF;
}

void plat_bta_output(int to_device)
{
    (void)to_device;
}

int plat_bta_out_device(void)
{
    return 1;
}

int plat_bta_lost(void)
{
    return PLAT_BTA_LOST_NONE;
}

int plat_bta_timed_out(void)
{
    return 0;
}

uint32_t plat_bta_mem_bytes(void)
{
    return 0;
}

int plat_bta_mem(void *p)
{
    (void)p;
    return -1;
}

int plat_bta_streams(void)
{
    return 0;
}

/* WiFi: off. */
void plat_wifi_on(int on)
{
    (void)on;
}

int plat_wifi_state(void)
{
    return WIFI_OFF;
}

const char *plat_wifi_why(void)
{
    return "";
}

const char *plat_wifi_ssid(void)
{
    return "";
}

uint32_t plat_wifi_ip(void)
{
    return 0;
}

int plat_wifi_rssi(void)
{
    return -127;
}

int plat_wifi_scan(void)
{
    return 0;
}

int plat_wifi_found(int i, char *ssid, int *rssi, int *secured)
{
    (void)i;
    (void)ssid;
    (void)rssi;
    (void)secured;
    return 0;
}

int plat_wifi_found_channel(int i)
{
    (void)i;
    return 0;
}

int plat_wifi_join(const char *ssid, const char *pass)
{
    (void)ssid;
    (void)pass;
    return -1;
}

int plat_wifi_saved(int i, char *ssid)
{
    (void)i;
    (void)ssid;
    return 0;
}

void plat_wifi_forget(int i)
{
    (void)i;
}

int plat_wifi_kept(const char *ssid)
{
    (void)ssid;
    return 0;
}

void plat_wifi_rx(uint32_t *frames, int *rssi)
{
    (void)frames;
    (void)rssi;
}

uint32_t plat_wifi_scans(void)
{
    return 0;
}

int plat_net_task(void (*fn)(void *), void *arg, const char *name)
{
    (void)fn;
    (void)arg;
    (void)name;
    return -1;
}

/* Locking: the IRQ flag is the lock here. */
uint32_t plat_lock(void)
{
    return irq_save();
}

void plat_unlock(uint32_t f)
{
    irq_restore(f);
}

void plat_yield(void)
{
}

void plat_restart(void)
{
    fm1_reboot();
}

void plat_log(const char *line)
{
    (void)line;
}

/* The flash data windows are not used by this port (the store has its own
 * sectors); the literal app does not call these. */
const uint8_t *plat_data(uint32_t off)
{
    (void)off;
    return 0;
}

int plat_data_erase(uint32_t off)
{
    (void)off;
    return -1;
}

int plat_data_write(uint32_t off, const void *src, uint32_t n)
{
    (void)off;
    (void)src;
    (void)n;
    return -1;
}

int plat_sysex_send(const uint8_t *frame, uint32_t n)
{
    (void)frame;
    (void)n;
    return -1;
}

uint32_t plat_sysex_room(void)
{
    return 0;
}

uint32_t plat_sysex_queued(void)
{
    return 0;
}

int plat_sysex_begin(void)
{
    return -1;
}

int plat_sysex_put(const uint8_t *p, uint32_t n)
{
    (void)p;
    (void)n;
    return -1;
}

int plat_sysex_end(void)
{
    return -1;
}

uint32_t plat_usb_session(void)
{
    return 0;
}

const uint8_t *plat_res(uint32_t *len)
{
    *len = 0;
    return 0;
}

int plat_console_open(void)
{
    return 0;
}

int plat_console_read(void)
{
    return -1;
}

uint32_t plat_console_room(void)
{
    return 0;
}

uint32_t plat_console_write(const void *p, uint32_t n)
{
    (void)p;
    (void)n;
    return 0;
}

void plat_console_present(int on)
{
    (void)on;
}

int plat_host_read(uint32_t addr, uint8_t *dst, uint32_t n)
{
    (void)addr;
    (void)dst;
    (void)n;
    return -1;
}

void plat_host_patience(uint32_t try_ms, uint32_t total_ms)
{
    (void)try_ms;
    (void)total_ms;
}

void plat_usb_audio_full(int on)
{
    (void)on;
}

void plat_usb_play_gain(uint32_t q12)
{
    (void)q12;
}

int16_t *plat_usb_ch(uint32_t off, uint32_t *run)
{
    (void)off;
    *run = 0;
    return 0;
}

uint32_t plat_cpu_khz(void)
{
    uint32_t khz = fm1_clock_khz();
    return khz ? khz : 360000u;
}

uint32_t plat_ticks24(void)
{
    return fm1_ticks();
}

void plat_disk_mode(const plat_disk_t *d)
{
    (void)d;
}

int plat_disk_poll(void)
{
    return PLAT_DISK_OFF;
}

uint32_t plat_disk_idle_ms(void)
{
    return 0;
}

void plat_code_window(const void *end)
{
    (void)end;
}

int plat_cpu2_set(int on)
{
    (void)on;
    return -1;
}

int plat_cpu2_on(void)
{
    return 0;
}

int plat_cpu2_run(void (*fn)(uint32_t), uint32_t arg)
{
    (void)fn;
    (void)arg;
    return 0;
}

int plat_cpu2_wait(void)
{
    return 0;
}

void plat_cpu2_sync(void)
{
}

int plat_cpu2_busy(void)
{
    return 0;
}

int plat_cpu2_gave_up(void)
{
    return 0;
}

uint32_t plat_cpu2_bad(void)
{
    return 0;
}

int plat_cpu2_barred(void)
{
    return 0;
}

uint32_t plat_cpu2_stack(void)
{
    return 0;
}

uint32_t plat_core(void)
{
    return 0;
}

const struct plat_cache_ram *plat_cache_ram(void)
{
    return 0;
}

void plat_stats_enable(int on)
{
    (void)on;
}

int plat_stats_on(void)
{
    return 0;
}

/* touch.c's remote keys: none on the Pocket AMP. */
static uint32_t plat_remote_keys(void)
{
    return 0;
}
