/* SPDX-License-Identifier: MIT */
#ifndef FUSA_TEST_SCALE_H
#define FUSA_TEST_SCALE_H
#include <stdint.h>
/* Keep the proven lower-2MiB source / upper-2MiB output layout.
 * Impose's observed 0x04154000 RGB32 buffer also fits below this boundary. */
#define FS_OUTPUT_BASE 0x04200000U
#define FS_OUTPUT_BYTES (768U*480U*2U)
typedef uint32_t FsWord __attribute__((may_alias));
static int fs_ram_source_valid(uintptr_t address,int stride,int format,unsigned start,unsigned end)
{
    unsigned p=(unsigned)address&0x1fffffffU;
    if(!address||(address&15)||(stride!=512&&stride!=1024)||format<0||format>3||
       start<0x08000000U||end>0x0c000000U||start>=end||p<start||p>=end)return 0;
    return (271U*stride+480U)*(format==3?4U:2U)<=end-p;
}
static uintptr_t fs_source_alias(uintptr_t address)
{
    unsigned p=(unsigned)address&0x1fffffffU;
    /* VRAM uses its uncached user alias; main RAM uses kernel KSEG1. */
    return p|(p>=0x08000000U?0xa0000000U:0x40000000U);
}
static int fs_blank_source(uintptr_t address,int stride,int format,int sync)
{
    return (!address || !stride) && format>=0 && format<=3 && (sync==0||sync==1);
}
static int fs_source_valid(uintptr_t address,int stride,int format)
{
    unsigned p=(unsigned)address&0x1fffffffU;
    return address && !(address&15) && (stride==512||stride==1024) &&
        format>=0 && format<=3 && p>=0x04000000U &&
        p+((271U*stride+480U)*(format==3?4U:2U))<=FS_OUTPUT_BASE;
}
static unsigned fs_output_format(unsigned format){return format==3?0:format;}
static uint32_t fs_rgb565(uint32_t p)
{return ((p>>3)&31U)|((p>>5)&0x7e0U)|((p>>8)&0xf800U);}
static void fs_copy32(uint16_t *out,const uint32_t *in,int stride)
{
    for(unsigned y=0;y<272;y++) {
        const volatile uint32_t *src=in+y*stride;
        FsWord *dst=(FsWord *)(out+y*480);
        for(unsigned x=0;x<480;x+=4) {
            uint32_t a=src[x],b=src[x+1],c=src[x+2],d=src[x+3];
            dst[x/2]=fs_rgb565(a)|(fs_rgb565(b)<<16);
            dst[x/2+1]=fs_rgb565(c)|(fs_rgb565(d)<<16);
        }
    }
}
/* Aligned little-endian VRAM words, preserving all 16-bit formats exactly.
 * Four source pixels a,b,c,d become a,a,b,c,c,d in three 32-bit stores.
 * Duplicate destination rows share register values, with no intermediate row.
 * may_alias permits the packed view of the original uint16_t pixels. */
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
/* Copy-to-copy pacing; no catch-up bursts. */
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
