/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef CONSOLIZER_POPS_SIGNATURE_H
#define CONSOLIZER_POPS_SIGNATURE_H
#include <stdint.h>
#include <string.h>
static inline int pops_serial_signature(const uint32_t *f,const uint32_t *c,uint32_t base) {
    static const uint32_t head[] = {
        0x00054040,0x27bdffc0,0x01053821,0xafb00020,
        0x00071900,0x03831021,0xafbf0030,0x24503c00,
        0x30c600ff,0xafb3002c,0xafb20028,0xafb10024,
        0x82070021,0x18e0000c,0x240201ff,0x24120001,
        0x50920099,0x92110026,0x1080001e,0x00000000,
        0x24050002,0x10850019,0x240d0042,0x92030020,
        0x506d0008,0x920e0025
    };
    if (memcmp(f,head,sizeof(head))) return 0;
    uint32_t target=((c[0]&0xffffu)<<16)+(int16_t)c[4];
    return (c[0]&0xffff0000u)==0x3c080000u && c[1]==0x24070001 &&
        c[2]==0x24030043 && c[3]==0x14c7001e &&
        (c[4]&0xffff0000u)==0x25020000u && target==base+0xa250 &&
        c[5]==0xae020020 && c[6]==0xa603001a && c[7]==0x8e020020 &&
        c[8]==0x30c600ff && c[9]==0x0040f809 && c[10]==0x308400ff;
}
#endif
