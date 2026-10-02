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
/* Aligned little-endian VRAM words, preserving all 16-bit formats exactly.
 * Four source pixels a,b,c,d become a,a,b,c,c,d in three 32-bit stores.
 * Duplicate destination rows share register values, with no intermediate row.
 * may_alias permits the packed view of the original uint16_t pixels. */
typedef uint32_t FsWord __attribute__((may_alias));
/* Worker-owned packed RAM copy. Volatile source reads bypass compiler reuse;
 * the caller additionally uses the uncached VRAM alias and validates capture. */
static void fs_copy16(uint16_t *out,const uint16_t *in,int stride)
{
    for(unsigned y=0;y<272;y++) {
        const volatile FsWord *src=(const volatile FsWord *)(in+y*stride);
        FsWord *dst=(FsWord *)(out+y*480);
        for(unsigned x=0;x<240;x+=8) {
            uint32_t a=src[x],b=src[x+1],c=src[x+2],d=src[x+3];
            uint32_t e=src[x+4],f=src[x+5],g=src[x+6],h=src[x+7];
            dst[x]=a;dst[x+1]=b;dst[x+2]=c;dst[x+3]=d;
            dst[x+4]=e;dst[x+5]=f;dst[x+6]=g;dst[x+7]=h;
        }
    }
}
static int fs_handoff_valid(unsigned ticket,unsigned held,unsigned before,unsigned after,int idle)
{
    return ticket&&ticket==held&&before==after&&idle;
}
/* Request-to-request period includes handoff wait; no catch-up bursts. */
static unsigned long long fs_next_frame(unsigned long long begin,unsigned long long end)
{
    return end<begin+33334ULL?begin+33334ULL:end; /* <=30 Hz preview ceiling */
}
static void fs_scale16(uint16_t *out,const uint16_t *in,int stride)
{
    unsigned y=0;
    for(unsigned sy=0;sy<272;sy++) {
        unsigned end=((sy+1)*480U+271U)/272U;
        const FsWord *src=(const FsWord *)(in+sy*stride);
        volatile FsWord *dst=(volatile FsWord *)(out+y*768);
        if(end-y==2) {
            volatile FsWord *second=dst+384;
            for(unsigned x=0;x<120;x++) {
                uint32_t ab=src[x*2],cd=src[x*2+1];
                uint32_t a=(ab&0xffffU)|((ab&0xffffU)<<16),b=(ab>>16)|(cd<<16);
                dst[x*3]=a;dst[x*3+1]=b;dst[x*3+2]=cd;
                second[x*3]=a;second[x*3+1]=b;second[x*3+2]=cd;
            }
        } else {
            for(unsigned x=0;x<120;x++) {
                uint32_t ab=src[x*2],cd=src[x*2+1];
                dst[x*3]=(ab&0xffffU)|((ab&0xffffU)<<16);
                dst[x*3+1]=(ab>>16)|(cd<<16);dst[x*3+2]=cd;
            }
        }
        y=end;
    }
}
#endif
