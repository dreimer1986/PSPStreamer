/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "protocol.h"
static const uint32_t sm_bt_targets[12]={0x4000,0x2000,0x8000,0x1000,0x100,0x200,1,8,0x10,0x20,0x40,0x80};
static inline SmBtProfile sm_bt_default_profile(void) {
    return (SmBtProfile){{1,2,3,4,5,6,9,10,17,18,19,20},0,1,0,0};
}
static inline int sm_bt_profile_valid(const SmBtProfile *p) {
    if(p->configured>1 || p->invert>3)return 0;
    if(!((p->axis_x<6 && p->axis_y<6 && p->axis_x!=p->axis_y) || (p->axis_x==255 && p->axis_y==255)))return 0;
    for(unsigned i=0;i<12;i++)if(p->binding[i]>20)return 0;
    return 1;
}
static inline uint32_t sm_bt_profile_buttons(const SmBtProfile *p,uint16_t raw,uint8_t hat) {
    uint32_t sources=raw|((uint32_t)(hat&15)<<16),result=0;
    for(unsigned i=0;i<12;i++)if(p->binding[i] && p->binding[i]<=20 && (sources&(1U<<(p->binding[i]-1))))result|=sm_bt_targets[i];
    return result;
}
static inline uint8_t sm_bt_profile_axis(const SmBtProfile *p,const uint8_t axes[6],unsigned valid,int vertical) {
    unsigned axis=vertical?p->axis_y:p->axis_x;
    if(axis>=6 || !(valid&(1U<<axis)))return 128;
    unsigned v=axes[axis];if(p->invert&(vertical?2:1))v=255-v;
    return v>=115 && v<=140?128:v;
}
static inline SmBtOptions sm_bt_default_options(void) {
    return (SmBtOptions){1,{0x4000,0x2000,0x8000,0x1000,0x100,0x200,0,0,1,8,0,0,0,0,0,0}};
}
static inline int sm_bt_options_valid(const SmBtOptions *o) {
    if(o->reconnect>1)return 0;
    for(unsigned i=0;i<16;i++) {
        uint32_t b=o->button[i];
        if((b&~0xf3f9U) || (b && (b&(b-1))))return 0;
    }
    return 1;
}
static inline uint32_t sm_bt_map_buttons(const SmBtOptions *o,uint16_t raw,uint32_t hat) {
    uint32_t buttons=hat&0xf0;
    for(unsigned i=0;i<16;i++)if(raw&(1U<<i))buttons|=o->button[i];
    return buttons;
}
