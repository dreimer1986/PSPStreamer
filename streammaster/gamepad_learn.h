/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "gamepad_options.h"
static inline unsigned sm_bt_single_source(const SmPad *pad) {
    uint32_t bits=pad->raw_buttons|((uint32_t)(pad->hat&15)<<16);
    if(!bits || (bits&(bits-1)))return 0;
    unsigned source=1;while(!(bits&1)){bits>>=1;source++;}return source;
}
static inline void sm_bt_assign(SmBtProfile *p,unsigned target,unsigned source) {
    if(target>=12 || source>20)return;
    for(unsigned i=0;i<12;i++)if(source && p->binding[i]==source)p->binding[i]=0;
    p->binding[target]=source;
}
typedef struct {uint8_t low[6],high[6],last[6],changes[6],seen,center;uint32_t sequence;} SmBtAxisLearn;
static inline int sm_bt_learn_axes(SmBtAxisLearn *a,const SmPad *p,unsigned elapsed_ms,uint8_t *x,uint8_t *y) {
    if(!p->connected){memset(a,0,sizeof(*a));return 0;}
    if(a->sequence!=p->sequence || !a->seen) {
        a->sequence=p->sequence;
        for(unsigned i=0;i<6;i++)if(p->axes_valid&(1U<<i)) {
            unsigned v=p->axes[i];
            if(!(a->seen&(1U<<i))){a->low[i]=a->high[i]=a->last[i]=v;a->seen|=1U<<i;}
            if(v<a->low[i])a->low[i]=v;
            if(v>a->high[i])a->high[i]=v;
            int diff=(int)v-a->last[i];if(diff>=4 || diff<=-4){if(a->changes[i]<255)a->changes[i]++;a->last[i]=v;}
        }
    }
    int axes[2]={-1,-1};unsigned count=0;
    for(unsigned i=0;i<6;i++)if((a->seen&(1U<<i)) && a->low[i]<=48 && a->high[i]>=207 && a->changes[i]>=6) {
        if(count<2)axes[count]=i;
        count++;
    }
    /* A circle must identify exactly two axes. Do not guess if several sticks
     * or triggers have been exercised together; user can restart the capture. */
    if(count!=2 || elapsed_ms<1500){a->center=0;return 0;}
    for(unsigned i=0;i<2;i++)if(p->axes[axes[i]]<96 || p->axes[axes[i]]>160){a->center=0;return 0;}
    if(++a->center<10)return 0;
    *x=axes[0];*y=axes[1];return 1;
}
