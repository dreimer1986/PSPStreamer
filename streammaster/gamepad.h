/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdint.h>
#include <string.h>
/* A bounded HID report descriptor reader. Unsupported layouts fail closed;
 * no guessed offsets from a controller marketing name. */
#define SM_HID_FIELDS 48
typedef struct {uint16_t bit,usage,page;uint8_t size,id;int32_t min,max;} SmHidField;
typedef struct {SmHidField field[SM_HID_FIELDS];unsigned count;int ids;} SmHidMap;
typedef struct {uint32_t buttons;uint8_t x,y;} SmHidPad;
static inline int32_t sm_hid_signed(uint32_t v,unsigned n) {
    return n==1?(int8_t)v:n==2?(int16_t)v:(int32_t)v;
}
static inline int sm_hid_parse(SmHidMap *m,const uint8_t *p,unsigned len) {
    struct Global {uint32_t page,size,count,id;int32_t min,max;} g={0},stack[4];
    unsigned depth=0,offset[256]={0},usages[64],nu=0,umin=0,umax=0;
    memset(m,0,sizeof(*m));
    if(len>4096)return 0;
    while(len) {
        unsigned tag=*p++,n=tag&3;len--;if(n==3)n=4;
        if(tag==254 || n>len)goto bad;
        uint32_t v=0;for(unsigned i=0;i<n;i++)v|=(uint32_t)p[i]<<(8*i);
        p+=n;len-=n;
        switch(tag&0xfc) {
        case 0x04:g.page=v;break;
        case 0x14:g.min=sm_hid_signed(v,n);break;
        case 0x24:g.max=g.min<0?sm_hid_signed(v,n):(int32_t)v;break;
        case 0x74:g.size=v;break;
        case 0x94:g.count=v;break;
        case 0x84:if(!v||v>255)goto bad;g.id=v;m->ids=1;break;
        case 0xa4:if(depth==4)goto bad;stack[depth++]=g;break;
        case 0xb4:if(!depth)goto bad;g=stack[--depth];break;
        case 0x08:if(nu==64)goto bad;usages[nu++]=v;break;
        case 0x18:umin=v;break;
        case 0x28:umax=v;break;
        case 0x80:
            if(g.size>32||g.count>512||g.size*g.count>4096-offset[g.id])goto bad;
            for(unsigned i=0;i<g.count;i++) {
                unsigned usage=i<nu?usages[i]:umin && umax>=umin && i<=umax-umin?umin+i:0;
                unsigned page=usage>65535?usage>>16:g.page;usage&=65535;
                if((v&3)==2 && g.size && ((page==9 && usage>=1 && usage<=16) ||
                    (page==1 && (usage==0x30 || usage==0x31 || usage==0x39)))) {
                    if(m->count==SM_HID_FIELDS || g.max<=g.min)goto bad;
                    m->field[m->count++]=(SmHidField){offset[g.id]+i*g.size,usage,page,g.size,g.id,g.min,g.max};
                }
            }
            offset[g.id]+=g.size*g.count;
            /* fall through */
        case 0x90:case 0xb0:case 0xa0:case 0xc0:nu=umin=umax=0;break;
        default:break;
        }
    }
    if(depth)goto bad;
    return m->count!=0;
bad:memset(m,0,sizeof(*m));return 0;
}
static inline int sm_hid_input(const SmHidMap *m,const uint8_t *p,unsigned len,SmHidPad *out) {
    unsigned id=0,matched=0;
    if(m->ids){if(!len)return 0;id=*p++;len--;}
    /* Common HID gamepad button order: south/east/west/north, L/R, triggers,
     * select/start. A device-specific mapping can be added after capture. */
    static const uint32_t buttons[16]={0x4000,0x2000,0x8000,0x1000,0x100,0x200,0,0,1,8,0,0,0,0,0,0};
    static const uint32_t hats[8]={0x10,0x30,0x20,0x60,0x40,0xc0,0x80,0x90};
    SmHidPad pad=*out;
    for(unsigned i=0;i<m->count;i++) {
        const SmHidField *f=&m->field[i];if(f->id!=id)continue;
        if(f->bit+f->size>len*8)return 0;
        uint32_t v=0;for(unsigned b=0;b<f->size;b++)v|=(uint32_t)((p[(f->bit+b)/8]>>((f->bit+b)%8))&1)<<b;
        int64_t value=f->min<0 && f->size<32 && (v&(1U<<(f->size-1)))?(int64_t)v-(1LL<<f->size):f->min<0?(int32_t)v:(int64_t)v;
        if(f->page==9){uint32_t mask=buttons[f->usage-1];pad.buttons=(pad.buttons&~mask)|(value?mask:0);}
        else if(f->usage==0x39){value-=f->min;if((int64_t)f->max-f->min==3)value*=2;
            pad.buttons=(pad.buttons&~0xf0U)|(value>=0&&value<8?hats[value]:0);}
        else {int64_t a=(value-f->min)*255/((int64_t)f->max-f->min);if(a<0)a=0;if(a>255)a=255;
            if(a>=115&&a<=140)a=128;
            if(f->usage==0x30)pad.x=a;else pad.y=a;}
        matched++;
    }
    if(matched)*out=pad;
    return matched!=0;
}
