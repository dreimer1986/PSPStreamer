/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "protocol.h"
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
