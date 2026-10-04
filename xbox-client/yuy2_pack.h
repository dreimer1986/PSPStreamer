/* SPDX-License-Identifier: GPL-2.0-or-later
 * Planar 4:2:0 -> packed YUY2 only; color conversion/scaling stay on NV2A.
 * Pentium III MMX/SSE stores bypass the cache for write-combined scanout RAM.
 */
#include <stdint.h>
#include <string.h>
static void xbox_pack_yuy2(uint8_t *dst,unsigned pitch,const uint8_t *y,unsigned ys,
                           const uint8_t *u,unsigned us,const uint8_t *v,unsigned vs,
                           unsigned width,unsigned height){
    for(unsigned row=0;row<height;row++){
        uint8_t *d=dst+row*pitch;const uint8_t *a=y+row*ys,*b=u+(row/2)*us,*c=v+(row/2)*vs;
        unsigned x=0;
        for(;x+8<=width;x+=8){
            /* Recent Clang implements the old MMX intrinsics with SSE2,
             * which the Xbox cannot execute. Pin these to real MMX/SSE1. */
            __asm__ __volatile__(
                "movq (%0), %%mm0\n\t"
                "movd (%1), %%mm1\n\t"
                "movd (%2), %%mm2\n\t"
                "punpcklbw %%mm2, %%mm1\n\t"
                "movq %%mm0, %%mm3\n\t"
                "punpcklbw %%mm1, %%mm0\n\t"
                "punpckhbw %%mm1, %%mm3\n\t"
                "movntq %%mm0, (%3)\n\t"
                "movntq %%mm3, 8(%3)"
                : : "r"(a+x),"r"(b+x/2),"r"(c+x/2),"r"(d+2*x)
                : "mm0","mm1","mm2","mm3","memory");
        }
        for(;x<width;x+=2){d[2*x]=a[x];d[2*x+1]=b[x/2];d[2*x+2]=a[x+1];d[2*x+3]=c[x/2];}
    }
    __asm__ __volatile__("sfence\n\temms" : : : "memory");
}
