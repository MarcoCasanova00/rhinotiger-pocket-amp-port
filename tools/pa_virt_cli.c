/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 RhinoTiger Pocket AMP port contributors */
/* The virtual-panel translator as a tiny stdin/stdout service, compiled from
 * the same source as the firmware (plat/pocketamp/pa_virt.c). The interactive
 * emulator (tools/emu_pa.py) and the regression scripts use it.
 *
 * Protocol:
 *   D <maskhex> <now_ms> [enc_dir] [master_pos]   update; answer:
 *       BTN 0x...      the synthesized B_* held mask
 *       KEY 0x...      the synthesized held keys
 *       E n0 n1 ... n6 accumulated EN_* detents (cleared)
 *   Q                                             quit
 */
#include <stdio.h>
#include <stdint.h>
#include "platform.h"
#include "plat.h"
#include "pa_virt.h"

int main(void)
{
    pa_virt_t v;
    char line[160];
    int i;
    pa_virt_init(&v);
    while (fgets(line, sizeof line, stdin)) {
        unsigned d, now;
        int e = 0, m = -1;
        if (sscanf(line, "D %x %u %d %d", &d, &now, &e, &m) >= 2) {
            if (m < 0)
                m = 4096;
            pa_virt_run(&v, (uint8_t)d, now, e, (uint32_t)m);
            printf("BTN 0x%x\nKEY 0x%x\n", pa_virt_buttons(&v), pa_virt_keys(&v));
            printf("E");
            for (i = 0; i < NE; i++)
                printf(" %d", pa_virt_take(&v, i));
            printf("\n");
            fflush(stdout);
        } else if (line[0] == 'Q' || line[0] == 'q') {
            break;
        }
    }
    return 0;
}
