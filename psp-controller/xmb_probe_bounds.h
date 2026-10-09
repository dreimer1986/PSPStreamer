/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef CONSOLIZER_XMB_PROBE_BOUNDS_H
#define CONSOLIZER_XMB_PROBE_BOUNDS_H
#include <stdint.h>
/* Only module segments inside ordinary PSP RAM; never MMIO, VRAM or an
 * unchecked end pointer. A capture is not a general-purpose memory dumper. */
static int xmb_probe_range(uint32_t address,uint32_t bytes) {
    uint32_t region=address&0xe0000000U,physical=address&0x1fffffffU;
    if(region!=0 && region!=0x80000000U)return 0;
    return bytes && bytes<=8U*1024*1024 && physical>=0x08000000U &&
        physical<0x0a000000U && bytes<=0x0a000000U-physical;
}
#endif
