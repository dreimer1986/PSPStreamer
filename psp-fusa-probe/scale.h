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
 * The local row costs 1440 bytes on the worker stack, not a frame allocation.
 * Four source pixels a,b,c,d become a,a,b,c,c,d in three 32-bit stores.
 * may_alias permits the packed view of the original uint16_t pixels. */
typedef uint32_t FsWord __attribute__((may_alias));
/* Worker-owned packed RAM copy. Volatile source reads bypass compiler reuse;
 * the caller additionally uses the uncached VRAM alias and validates capture. */
static void fs_copy16(uint16_t *out,const uint16_t *in,int stride)
{
    for(unsigned y=0;y<272;y++) {
        const volatile FsWord *src=(const volatile FsWord *)(in+y*stride);
        FsWord *dst=(FsWord *)(out+y*480);
        for(unsigned x=0;x<240;x++)dst[x]=src[x];
    }
}
static int fs_handoff_valid(unsigned ticket,unsigned held,unsigned before,unsigned after,int idle)
{
    return ticket&&ticket==held&&before==after&&idle;
}
/* Fixed start-to-start period; never burst to catch up after a slow frame. */
static unsigned long long fs_next_frame(unsigned long long begin,unsigned long long end)
{
    return end<begin+83333ULL?begin+83333ULL:end;
}
static void fs_scale16(uint16_t *out,const uint16_t *in,int stride)
{
    uint32_t row[360];
    unsigned previous=~0U;
    for(unsigned y=0;y<480;y++) {
        unsigned source_y=y*272U/480U;
        if(source_y!=previous) {
            const FsWord *src=(const FsWord *)(in+source_y*stride);
            for(unsigned x=0;x<120;x++) {
                uint32_t ab=src[x*2],cd=src[x*2+1];
                row[x*3]=(ab&0xffffU)|((ab&0xffffU)<<16);
                row[x*3+1]=(ab>>16)|(cd<<16);
                row[x*3+2]=cd;
            }
            previous=source_y;
        }
        volatile FsWord *dst=(volatile FsWord *)(out+y*768);
        for(unsigned x=0;x<360;x++)dst[x]=row[x];
    }
}
#endif
