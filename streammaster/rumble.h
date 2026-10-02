/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef SM_RUMBLE_H
#define SM_RUMBLE_H
#include <stdint.h>
#include <stddef.h>
#include <string.h>
/* EP0 input transaction reply. No native struct layout on the wire. */
#define SM_RUMBLE_BYTES 8
static inline int sm_rumble_fresh(uint64_t now,uint64_t stamp) {
    return stamp && now>=stamp && now-stamp<250000;
}
static inline void sm_rumble_frame(uint8_t out[8], unsigned small, unsigned large) {
    const uint8_t blank[8]={'R','M',1,0,0,0,0,0};
    memcpy(out,blank,8);out[3]=!!small;out[4]=(uint8_t)large;
}
static inline int sm_rumble_valid(const uint8_t *p,size_t n) {
    return p && n==8 && p[0]=='R' && p[1]=='M' && p[2]==1 &&
        p[3]<=1 && !p[5] && !p[6] && !p[7];
}
/* Report format: Linux hid-microsoft.c / SDL_hidapi_xboxone.c.
 * A finite 200 ms effect, refreshed while alive; never an endless loop.
 * PS1 small motor is binary; the large motor has 256 intensity levels. */
static inline void sm_rumble_xbox(uint8_t out[9],unsigned small,unsigned large) {
    const uint8_t blank[9]={3,3,0,0,0,0,20,0,0};
    memcpy(out,blank,9);out[4]=(large*100u+127u)/255u;out[5]=small?100:0;
}
/* Only accept known XInput Bluetooth identities with exactly the expected
 * report length. Unknown USB adapters never receive guessed motor writes. */
static inline int sm_rumble_xbox_descriptor(unsigned vendor,unsigned product,const uint8_t *d,size_t n) {
    if(vendor!=0x045e || (product!=0x02e0 && product!=0x02fd) || !d)return 0;
    struct {unsigned size,count,id;} g={0},stack[8];
    unsigned depth=0,bits=0;
    for(size_t i=0;i<n;) {
        unsigned tag=d[i++],len=tag&3; if(len==3)len=4;
        if(tag==0xfe || len>n-i)return 0;
        uint32_t value=0;for(unsigned j=0;j<len;j++)value|=(uint32_t)d[i++]<<(8*j);
        switch(tag&0xfc) {
        case 0x74:g.size=value;break;
        case 0x94:g.count=value;break;
        case 0x84:g.id=value;break;
        case 0xa4:if(depth==8)return 0;stack[depth++]=g;break;
        case 0xb4:if(!depth)return 0;g=stack[--depth];break;
        case 0x90:
            if(g.id==3){if(g.size>64 || g.count>64 || g.size*g.count>64-bits)return 0;bits+=g.size*g.count;}
            break;
        }
    }
    return !depth && bits==64;
}
#endif
