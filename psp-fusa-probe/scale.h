/* SPDX-License-Identifier: MIT */
#ifndef FUSA_TEST_SCALE_H
#define FUSA_TEST_SCALE_H
#include <stdint.h>
/* First hardware test deliberately accepts only lower-2MiB VRAM, 16-bit
 * 480x272 frames, matching the captured game. Never infer spare game VRAM. */
static int fs_source_valid(uintptr_t address,int stride,int format)
{
    unsigned p=(unsigned)address&0x1fffffffU;
    return address && !(address&15) && (stride==512||stride==1024) &&
        format>=0 && format<=2 && p>=0x04000000U &&
        p+((271U*stride+480U)*2U)<=0x04200000U;
}
/* Nearest neighbour keeps the original 16-bit format (including 5551).
 * No GE commands, no game GPU state to save/restore. */
static void fs_scale16(uint16_t *out,const uint16_t *in,int stride)
{
    for(unsigned y=0;y<480;y++) {
        const uint16_t *src=in+(y*272U/480U)*stride;
        uint16_t *dst=out+y*768;
        for(unsigned x=0;x<240;x++) {
            uint16_t a=src[x*2],b=src[x*2+1];
            dst[x*3]=a;dst[x*3+1]=a;dst[x*3+2]=b;
        }
    }
}
#endif
