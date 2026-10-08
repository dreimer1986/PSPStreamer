/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef POPS_AUDIO_SHARED_H
#define POPS_AUDIO_SHARED_H
#include <stdint.h>
#include <stddef.h>
#define POPS_AUDIO_RING 4096U
typedef struct {
    uint32_t original;
    volatile uint32_t enabled,wr,rd,dropped,calls;
    uint32_t reserved[10];
    volatile uint32_t pcm[POPS_AUDIO_RING];
} PopsAudioShared;
_Static_assert(offsetof(PopsAudioShared,pcm)==64,"ME payload layout");
/* No imports, GP references, allocations or kernel calls. Only t0..t2/t9
 * scratch registers and RA/stack are used; callback return v0/v1 preserved.
 * Execute the original callback first. Capture is one returned stereo word.
 * The four patched immediates supply original callback and UNCACHED context.
 */
static inline void pops_audio_code(uint32_t *p,uint32_t original,uint32_t context) {
    const uint32_t code[]={
        0x27bdfff0,0xafbf0000,0x3c190000,0x37390000,0x0320f809,0,
        0x3c080000,0x35080000,
        0x8d090014,0x25290001,0xad090014, /* calls */
        0x8d090004,0x11200013,0,       /* disabled -> return (32) */
        0x8d090008,0x8d0a000c,0x012a5023,
        0x2d4a1000,0x1140000a,0,      /* full -> drop (29) */
        0x312a0fff,0x000a5080,0x010a5021,0xad420040,
        0x0000000f,0x25290001,0xad090008,0x10000004,
        0,                          /* branch delay / drop starts at29 */
        0x8d090010,0x25290001,0xad090010,
        0x8fbf0000,0x03e00008,0x27bd0010
    };
    for(unsigned i=0;i<sizeof(code)/4;i++)p[i]=code[i];
    p[2]|=original>>16;p[3]|=original&65535;
    p[6]|=context>>16;p[7]|=context&65535;
}
#define POPS_AUDIO_CODE_BYTES (35U*4)
static inline uint32_t pops_audio_hash(const uint32_t *code,uint32_t base,unsigned lo,unsigned hi) {
    uint32_t h=2166136261U;
    for(unsigned off=lo;off<hi;off+=4) {
        uint32_t w=code[off/4];
        if(off==0x2fc8 || off==0x2fcc || off==0x34bc || off==0x34c0 || off==0x34d0 || off==0x34f0)w&=0xffff0000U;
        if(w>>26==2 || w>>26==3) {
            uint32_t target=((base+off+4)&0xf0000000U)|((w&0x03ffffffU)<<2);
            if(target<base || target-base>=18832)return 0;
            w=(w&0xfc000000U)|((target-base)/4);
        }
        for(unsigned b=0;b<4;b++)h=(h^((w>>(b*8))&255))*16777619U;
    }
    return h;
}
#endif
