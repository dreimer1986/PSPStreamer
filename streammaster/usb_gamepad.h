/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <stdint.h>
#include <stddef.h>
/* Select a non-boot HID interface, with a bounded report descriptor and one
 * interrupt IN endpoint. Other interfaces (keyboard/consumer controls) are
 * never claimed. The report descriptor must additionally describe a gamepad. */
typedef struct {int interface;unsigned endpoint,packet,report_bytes;} SmUsbGamepad;
static inline int sm_usb_gamepad_interface(const uint8_t *p,size_t n,SmUsbGamepad *out) {
    SmUsbGamepad c={.interface=-1};
    for(size_t at=0;at+2<=n;) {
        unsigned len=p[at];const uint8_t *d=p+at;
        if(len<2||len>n-at)return 0;
        if(d[1]==4 && len>=9) {
            if(c.interface>=0&&c.endpoint&&c.report_bytes){*out=c;return 1;}
            c=(SmUsbGamepad){.interface=d[3]==0&&d[5]==3&&d[6]==0&&d[7]==0?d[2]:-1};
        } else if(c.interface>=0 && d[1]==0x21 && len>=9) {
            for(unsigned i=0;i<d[5] && 6+3*i+3<=len;i++) {
                unsigned off=6+3*i,bytes=d[off+1]|d[off+2]<<8;
                if(d[off]==0x22&&bytes&&bytes<=1024)c.report_bytes=bytes;
            }
        } else if(c.interface>=0 && d[1]==5 && len>=7) {
            unsigned packet=d[4]|d[5]<<8;
            if((d[2]&0x80)&&(d[3]&3)==3&&packet&&packet<=64){c.endpoint=d[2];c.packet=packet;}
        }
        at+=len;
    }
    if(c.interface<0||!c.endpoint||!c.report_bytes)return 0;
    *out=c;return 1;
}
static inline int sm_usb_gamepad_descriptor(const uint8_t *p,size_t n) {
    unsigned page=0,usage=0;
    while(n) {
        unsigned tag=*p++,len=tag&3;n--;if(len==3)len=4;
        if(tag==254||len>n)return 0;
        unsigned v=0;for(unsigned i=0;i<len;i++)v|=(unsigned)p[i]<<(8*i);
        if((tag&0xfc)==0x04)page=v;
        if((tag&0xfc)==0x08)usage=v;
        if((tag&0xfc)==0xa0 && v==1 && page==1 && (usage==4||usage==5))return 1;
        p+=len;n-=len;
    }
    return 0;
}
