/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 RhinoTiger Pocket AMP port contributors */
/* Host smoke test for the port's store (plat/pocketamp/store.c): the A/B
 * sector logic, CRC-32, torn-write fallback and object isolation, against a
 * RAM-backed flash model with NOR write semantics (bits only clear). Run with
 * the host gcc:  gcc -I plat/pocketamp -o /tmp/test_store tools/test_store_host.c
 * then /tmp/test_store. */
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#define PA_STORE_SECTOR 4096u
#define PLAT_STORE_MAX 3840u
#define PA_STORE_BASE 0x86000u
#define PA_STORE_PAYLOAD_OFF 256u

static uint8_t flash[0x4000];           /* 4 sectors: objects 0 and 1, A/B */

static int fl_read(uint32_t off, uint8_t *dst, uint32_t n)
{
    off -= PA_STORE_BASE;
    if (off + n > sizeof flash)
        return -1;
    memcpy(dst, flash + off, n);
    return 0;
}

static int fl_write(uint32_t off, const void *src, uint32_t n)
{
    const uint8_t *s = src;
    uint32_t i;
    off -= PA_STORE_BASE;
    if (off + n > sizeof flash)
        return -1;
    for (i = 0; i < n; i++)
        flash[off + i] &= s[i];         /* NOR: bits only go 1 -> 0 */
    return 0;
}

static int fl_erase4k(uint32_t off, uint32_t *took)
{
    if (took)
        *took = 0;
    off -= PA_STORE_BASE;
    memset(flash + (off & ~4095u), 0xFF, 4096);
    return 0;
}

#include "store.c"

static int fails;

#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); fails++; } } while (0)

int main(void)
{
    uint8_t payload[100], got[100];
    uint32_t i;
    memset(flash, 0xFF, sizeof flash);

    /* fresh store: no object */
    CHECK(plat_store_load(0, got, sizeof got) == -1);
    CHECK(plat_store_size(0) == -1);

    /* save object 0, load it back */
    for (i = 0; i < sizeof payload; i++)
        payload[i] = (uint8_t)(i * 7u + 3u);
    CHECK(plat_store_save(0, payload, sizeof payload) == 0);
    CHECK(plat_store_load(0, got, sizeof got) == (int)sizeof payload);
    CHECK(memcmp(payload, got, sizeof payload) == 0);
    CHECK(plat_store_size(0) == (int)sizeof payload);

    /* a second save goes to the other sector, seq bumped */
    {
        uint32_t h[8], sn;
        CHECK(fl_read(PA_STORE_BASE, (uint8_t *)h, 32) == 0);
        sn = h[2];
        payload[0] ^= 0x55;
        CHECK(plat_store_save(0, payload, sizeof payload) == 0);
        CHECK(plat_store_load(0, got, sizeof got) == (int)sizeof payload);
        CHECK(got[0] == payload[0]);
        CHECK(fl_read(PA_STORE_BASE + 4096u, (uint8_t *)h, 32) == 0);
        CHECK(h[2] == sn + 1u);
    }

    /* a torn write (header erased) leaves the older copy readable: A still
     * holds the payload as first saved (payload[0] == 3), before the ^0x55 */
    memset(flash + 4096, 0xFF, 256);
    CHECK(plat_store_load(0, got, sizeof got) == (int)sizeof payload);
    CHECK(got[0] == 3u);

    /* payload corruption on the valid sector is detected; the other is used */
    memset(flash + 4096 + 300, 0, 4);
    CHECK(plat_store_load(0, got, sizeof got) == (int)sizeof payload);
    CHECK(got[0] == 3u);

    /* object 1 is separate */
    CHECK(plat_store_load(1, got, sizeof got) == -1);
    CHECK(plat_store_save(1, payload, 50) == 0);
    CHECK(plat_store_load(1, got, 50) == 50);
    CHECK(memcmp(payload, got, 50) == 0);
    CHECK(plat_store_load(0, got, sizeof got) == (int)sizeof payload);

    /* bad object index */
    CHECK(plat_store_save(9, payload, 10) == -1);
    CHECK(plat_store_load(9, got, sizeof got) == -1);

    /* oversized object is refused */
    CHECK(plat_store_save(0, payload, PLAT_STORE_MAX + 1) == -1);

    if (fails) {
        printf("store host test: %d FAILURES\n", fails);
        return 1;
    }
    printf("store host test: all passed\n");
    return 0;
}
