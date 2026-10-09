/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef CONSOLIZER_XMB_PROBE_BOUNDS_H
#define CONSOLIZER_XMB_PROBE_BOUNDS_H
#include <stdint.h>
/* Only module segments inside ordinary PSP RAM; never MMIO, VRAM or an
 * unchecked end pointer. A capture is not a general-purpose memory dumper. */
static uint32_t xmb_probe_ram_end(int model) {
    /* 01g has 32 MiB, known later physical PSP models have 64 MiB.
     * Unknown hardware retains the conservative 32 MiB bound. */
    return model>=1 && model<=10?0x0c000000U:0x0a000000U;
}
static int xmb_probe_range(uint32_t address,uint32_t bytes,uint32_t ram_end) {
    uint32_t region=address&0xe0000000U,physical=address&0x1fffffffU;
    if(region!=0 && region!=0x80000000U)return 0;
    if(ram_end!=0x0a000000U && ram_end!=0x0c000000U)return 0;
    return bytes && bytes<=8U*1024*1024 && physical>=0x08000000U &&
        physical<ram_end && bytes<=ram_end-physical;
}
#endif
