/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef CONSOLIZER_AUDIO_SRC_H
#define CONSOLIZER_AUDIO_SRC_H
#include <stdint.h>
#include <string.h>
#define MIRROR_SRC_RING 8192U
/* Power-of-two rings, at most one ring of frames per operation. Two bounded
 * bulk copies avoid a mask/branch and memcpy call for every stereo sample. */
static inline void mirror_ring_read(void *out,const unsigned *ring,unsigned capacity,unsigned rd,unsigned frames) {
    unsigned at=rd&(capacity-1),first=capacity-at;
    if(first>frames)first=frames;
    memcpy(out,ring+at,first*4);
    if(first<frames)memcpy((unsigned char *)out+first*4,ring,(frames-first)*4);
}
static inline void mirror_ring_write(unsigned *ring,unsigned capacity,unsigned wr,const void *in,unsigned frames) {
    unsigned at=wr&(capacity-1),first=capacity-at;
    if(first>frames)first=frames;
    memcpy(ring+at,in,first*4);
    if(first<frames)memcpy(ring,(const unsigned char *)in+first*4,(frames-first)*4);
}
static inline unsigned mirror_src_rate(unsigned rate) {
    switch(rate) {
    case 32000: case 44100: case 48000: return rate;
    case 8000: case 11025: case 12000: case 16000: case 22050: case 24000: return 48000;
    default: return 0;
    }
}
static inline int mirror_src_gain(int sample,unsigned volume) {
    int value=sample*(int)(volume>>5)/1024;
    return value < -32768 ? -32768 : value > 32767 ? 32767 : value;
}
static inline uint32_t mirror_src_sample(uint32_t word,unsigned volume) {
    return (uint16_t)mirror_src_gain((int16_t)word,volume) |
        ((uint32_t)(uint16_t)mirror_src_gain((int16_t)(word>>16),volume)<<16);
}
/* Integer linear interpolation, with phase in output-rate units. No floating
 * point in kernel code; the worker retains phase across USB packet boundaries. */
static inline uint32_t mirror_src_lerp(uint32_t a,uint32_t b,unsigned phase,unsigned rate) {
    int l=(int16_t)a, r=(int16_t)(a>>16);
    l+=(int)(((int64_t)(int16_t)b-l)*phase/rate);
    r+=(int)(((int64_t)(int16_t)(b>>16)-r)*phase/rate);
    return (uint16_t)l|((uint32_t)(uint16_t)r<<16);
}
static inline int mirror_src_signature(const uint32_t *code,size_t bytes,uint32_t base,uint32_t state) {
    if(bytes!=13588)return 0;
    const unsigned hi[]={0x212c,0x212c,0x2264,0x22cc,0x22cc,0x22cc,0x22cc};
    const unsigned lo[]={0x2130,0x21bc,0x2268,0x22d0,0x2384,0x2398,0x23f8};
    for(unsigned i=0;i<7;i++) {
        uint32_t address=((code[hi[i]/4]&65535U)<<16)+(int16_t)code[lo[i]/4];
        if(address!=state+(i==2?1088:0))return 0;
    }
    if(mirror_jtarget(code[0x2170/4],base+0x2170)!=base+0x225c || code[0x2174/4]!=0x00408021)return 0;
    uint32_t h=2166136261U;
    for(unsigned off=0x20ec;off<0x2454;off+=4) {
        uint32_t word=code[off/4];
        for(unsigned i=0;i<7;i++)if(off==hi[i] || off==lo[i]){word&=0xffff0000U;break;}
        if(word>>26==2 || word>>26==3) {
            uint32_t target=mirror_jtarget(word,base+off);
            if(target<base || target-base>=bytes)return 0;
            word=(word&0xfc000000U)|((target-base)/4);
        }
        for(unsigned b=0;b<4;b++)h=(h^((word>>(8*b))&255))*16777619U;
    }
    return h==0x68840b6dU;
}
#endif
