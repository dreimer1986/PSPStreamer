/* Pentium III / SSE1 block copy. The pinned SDK memcpy is a byte loop;
 * especially WC framebuffer reads must not use one transaction per byte.
 * Non-overlapping regions only, like memcpy; never reads beyond the range. */
#ifndef XBOX_VISUAL_COPY_H
#define XBOX_VISUAL_COPY_H
#include <stddef.h>
static inline void xbox_visual_copy(void *destination,const void *source,size_t bytes){
    unsigned char *d=destination;const unsigned char *s=source;
    while(bytes>=64){
        __asm__ volatile(
            "movups 0(%1), %%xmm0\n\t"
            "movups 16(%1), %%xmm1\n\t"
            "movups 32(%1), %%xmm2\n\t"
            "movups 48(%1), %%xmm3\n\t"
            "movups %%xmm0, 0(%0)\n\t"
            "movups %%xmm1, 16(%0)\n\t"
            "movups %%xmm2, 32(%0)\n\t"
            "movups %%xmm3, 48(%0)"
            ::"r"(d),"r"(s):"xmm0","xmm1","xmm2","xmm3","memory");
        d+=64;s+=64;bytes-=64;
    }
    while(bytes>=16){
        __asm__ volatile("movups (%1), %%xmm0; movups %%xmm0, (%0)"
            ::"r"(d),"r"(s):"xmm0","memory");
        d+=16;s+=16;bytes-=16;
    }
    /* REP prevents LLVM from recognizing this tail as another memcpy call. */
    __asm__ volatile("cld; rep movsb":"+D"(d),"+S"(s),"+c"(bytes)::"memory","cc");
}
#endif
