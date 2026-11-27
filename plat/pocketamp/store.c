/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 RhinoTiger Pocket AMP port contributors */
/* The persistent store for the literal app's objects (OBJ_SETTINGS,
 * OBJ_PANEL): A/B 4 KiB sector pairs in the port's writable flash window,
 * payload at offset 256, a 32-byte header written last, CRC-32, the highest
 * seq wins. This replaces the literal APP_STORE_MAP {0xFC000u, 0xDC000u},
 * which is not safe on the Pocket AMP (read-only areas); the objects are
 * identical. New port code only. */
#include "platform.h"

typedef struct {
    uint32_t magic, obj, seq, len, crc;
    uint32_t pad[3];                    /* 32 bytes total */
} pa_store_head_t;

static uint8_t pa_store_scratch[PA_STORE_SECTOR];

static uint32_t pa_crc32(const uint8_t *p, uint32_t n)
{
    uint32_t crc = 0xFFFFFFFFu, i, b;
    for (i = 0; i < n; i++) {
        crc ^= p[i];
        for (b = 0; b < 8u; b++)
            crc = (crc >> 1) ^ (0xEDB88320u & -(crc & 1u));
    }
    return ~crc;
}

static uint32_t pa_store_base(uint32_t obj)
{
    return obj < PA_STORE_NOBJ ? PA_STORE_BASE + obj * 2u * PA_STORE_SECTOR : 0u;
}

static int pa_store_read_head(uint32_t off, pa_store_head_t *h)
{
    if (fl_read(off, (uint8_t *)h, sizeof *h))
        return -1;
    if (h->magic != PA_STORE_MAGIC || h->len > PLAT_STORE_MAX)
        return -1;
    return 0;
}

int plat_store_load(uint32_t obj, void *dst, uint32_t max)
{
    uint32_t base = pa_store_base(obj), seq = 0, len = 0;
    int have = 0, i;
    if (!base || !dst)
        return -1;
    for (i = 0; i < 2; i++) {
        pa_store_head_t h;
        if (pa_store_read_head(base + (uint32_t)i * PA_STORE_SECTOR, &h) || h.obj != obj ||
            h.len > max || h.len > PLAT_STORE_MAX)
            continue;
        if (fl_read(base + (uint32_t)i * PA_STORE_SECTOR + PA_STORE_PAYLOAD_OFF,
                    pa_store_scratch, h.len))
            continue;
        if (pa_crc32(pa_store_scratch, h.len) != h.crc)
            continue;
        if (!have || h.seq >= seq) {    /* ties: the A sector */
            have = 1;
            seq = h.seq;
            len = h.len;
            memcpy(dst, pa_store_scratch, h.len);
        }
    }
    return have ? (int)len : -1;
}

int plat_store_head(uint32_t obj, void *dst, uint32_t n)
{
    uint32_t base = pa_store_base(obj), seq = 0;
    int have = 0, i;
    if (!base || !dst)
        return -1;
    for (i = 0; i < 2; i++) {
        pa_store_head_t h;
        if (pa_store_read_head(base + (uint32_t)i * PA_STORE_SECTOR, &h) || h.obj != obj)
            continue;
        if (!have || h.seq >= seq) {
            have = 1;
            seq = h.seq;
            if (n > h.len)
                n = h.len;
            if (n && fl_read(base + (uint32_t)i * PA_STORE_SECTOR + PA_STORE_PAYLOAD_OFF,
                             dst, n))
                return -1;
        }
    }
    return have ? (int)n : -1;
}

int plat_store_size(uint32_t obj)
{
    uint32_t base = pa_store_base(obj), seq = 0, len = 0;
    int have = 0, i;
    if (!base)
        return -1;
    for (i = 0; i < 2; i++) {
        pa_store_head_t h;
        if (pa_store_read_head(base + (uint32_t)i * PA_STORE_SECTOR, &h) || h.obj != obj)
            continue;
        if (!have || h.seq >= seq) {
            have = 1;
            seq = h.seq;
            len = h.len;
        }
    }
    return have ? (int)len : -1;
}

int plat_store_save(uint32_t obj, const void *src, uint32_t len)
{
    uint32_t base = pa_store_base(obj), seq = 0, target, off, took;
    pa_store_head_t h;
    int i, rc;
    if (!base || !src || len > PLAT_STORE_MAX)
        return -1;
    for (i = 0; i < 2; i++) {
        pa_store_head_t t;
        if (!pa_store_read_head(base + (uint32_t)i * PA_STORE_SECTOR, &t) && t.obj == obj && t.seq > seq)
            seq = t.seq;
    }
    /* the older sector holds the next copy; a tie goes to A */
    target = seq & 1u;
    off = base + target * PA_STORE_SECTOR;
    rc = fl_erase4k(off, &took);
    if (rc)
        return rc;
    rc = fl_write(off + PA_STORE_PAYLOAD_OFF, src, len);
    if (rc)
        return rc;
    h.magic = PA_STORE_MAGIC;
    h.obj = obj;
    h.seq = seq + 1u;
    h.len = len;
    h.crc = pa_crc32(src, len);
    h.pad[0] = h.pad[1] = h.pad[2] = 0;
    return fl_write(off, (const uint8_t *)&h, sizeof h);
}

int plat_store_room(void)
{
    return -1;                          /* no pooled objects */
}
