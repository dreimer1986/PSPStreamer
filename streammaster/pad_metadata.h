/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "protocol.h"
typedef struct {SmPadMeta partial;unsigned next;} SmPadMetaRx;
/* Only publish a complete, ordered snapshot. Missing chunks cannot combine
 * two devices' metadata. USB control requests on EP0 complete in order. */
static inline int sm_pad_meta_receive(SmPadMetaRx *rx,unsigned chunk,uint16_t low,uint16_t high,SmPadMeta *out) {
    if(!chunk)rx->next=0;
    if(chunk>=16 || chunk!=rx->next){rx->next=0;return 0;}
    uint8_t *v=(uint8_t *)&rx->partial+chunk*4;
    v[0]=low;v[1]=low>>8;v[2]=high;v[3]=high>>8;
    if(++rx->next!=16)return 0;
    rx->next=0;
    if(rx->partial.valid!=1 || rx->partial.state>SM_BT_ERROR)return 0;
    *out=rx->partial;out->name[47]=0;
    if(out->battery>100)out->battery=255;
    return 1;
}
