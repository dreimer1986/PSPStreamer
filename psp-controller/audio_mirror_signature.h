/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef CONSOLIZER_AUDIO_MIRROR_SIGNATURE_H
#define CONSOLIZER_AUDIO_MIRROR_SIGNATURE_H
#include <stdint.h>
#include <stddef.h>
/* Exact 6.61 driver observed on hardware. Normalize only known relocations;
 * no wildcards for opcodes, branches, loop sizes or the original delay slot. */
static inline uint32_t mirror_jtarget(uint32_t word,uint32_t pc) {
    return ((pc+4)&0xf0000000U)|((word&0x03ffffffU)<<2);
}
static inline int mirror_dma_half(uint32_t link0,uint32_t link1,uint32_t physical) {
    if(link0==0 && link1==physical+1024)return 0;
    if(link1==0 && link0==physical+1056)return 1;
    return -1;
}
static inline int mirror_signature(const uint32_t *code,size_t bytes,uint32_t base,uint32_t state) {
    if(bytes!=13588 || (base&3) || base<0x88000000U || base>0x8bff0000U)return 0;
    uint32_t address=(code[0x2c0/4]&65535U)<<16;
    address+=(int16_t)code[0x2d0/4];
    if(address!=state || (state&63) || state<base+bytes || state>0x8bfff000U)return 0;
    if(((code[0x464/4]&65535U)!=(code[0x2d0/4]&65535U)) ||
       mirror_jtarget(code[0x46c/4],base+0x46c)!=base+0x2f10)return 0;
    uint32_t h=2166136261U;
    for(unsigned off=0x2b8;off<0x530;off+=4) {
        uint32_t word=code[off/4];
        if(off==0x2c0 || off==0x2d0 || off==0x464)word&=0xffff0000U;
        if(word>>26==2 || word>>26==3) {
            uint32_t target=mirror_jtarget(word,base+off);
            if(target<base || target-base>=bytes)return 0;
            word=(word&0xfc000000U)|((target-base)/4);
        }
        for(unsigned b=0;b<4;b++)h=(h^((word>>(b*8))&255))*16777619U;
    }
    return h==0x01b648c2U && code[0x2f10/4]>>26==2 && code[0x2f14/4]==0;
}
#endif
