/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include "streammaster/gamepad.h"
#include "streammaster/protocol.h"
int main(void) {
    _Static_assert(sizeof(SmPad)==32,"pad wire size");
    _Static_assert(sizeof(SmBtStatus)==476,"status wire size");
    _Static_assert(sizeof(SmBtAction)==12,"action wire size");
    const uint8_t descriptor[]={
        0x05,1,0x09,5,0xa1,1, /* Gamepad */
        0x05,9,0x19,1,0x29,10,0x15,0,0x25,1,0x75,1,0x95,10,0x81,2,
        0x75,6,0x95,1,0x81,3, /* padding */
        0x05,1,0x09,0x30,0x09,0x31,0x15,0,0x26,255,0,0x75,8,0x95,2,0x81,2,
        0x09,0x39,0x15,0,0x25,7,0x75,4,0x95,1,0x81,0x42,
        0x75,4,0x95,1,0x81,3,0xc0};
    SmHidMap m;SmHidPad pad={0,128,128};
    assert(sm_hid_parse(&m,descriptor,sizeof(descriptor))&&m.count==13);
    uint8_t report[]={0x31,3,0,255,1};
    assert(sm_hid_input(&m,report,sizeof(report),&pad));
    assert(pad.buttons==(0x4000|0x100|0x200|1|8|0x30)&&pad.x==0&&pad.y==255);
    SmHidPad before=pad;
    assert(!sm_hid_input(&m,report,2,&pad));assert(!memcmp(&pad,&before,sizeof(pad)));
    uint8_t released[]={0,0,128,127,15};
    assert(sm_hid_input(&m,released,sizeof(released),&pad));assert(!pad.buttons&&pad.x==128&&pad.y==128);
    const uint8_t ids[]={0x05,1,0x85,1,0x09,0x30,0x15,0x81,0x25,127,0x75,8,0x95,1,0x81,2,
                         0x85,2,0x09,0x31,0x81,2};
    assert(sm_hid_parse(&m,ids,sizeof(ids)));
    uint8_t x[]={1,0x81},y[]={2,127},unknown[]={3,0};
    assert(sm_hid_input(&m,x,2,&pad)&&pad.x==0&&pad.y==128);
    assert(sm_hid_input(&m,y,2,&pad)&&pad.x==0&&pad.y==255);
    assert(!sm_hid_input(&m,unknown,2,&pad));
    const uint8_t bad1[]={0xb4},bad2[]={0x75,33,0x95,1,0x81,2},bad3[]={0xfe},bad4[]={0x26,0};
    assert(!sm_hid_parse(&m,bad1,sizeof(bad1))&&!m.count);
    assert(!sm_hid_parse(&m,bad2,sizeof(bad2))&&!m.count);
    assert(!sm_hid_parse(&m,bad3,sizeof(bad3))&&!m.count);
    assert(!sm_hid_parse(&m,bad4,sizeof(bad4))&&!m.count);
    /* Deterministic malformed descriptors and reports under ASan/UBSan. */
    unsigned seed=0x13579;uint8_t fuzz[128];
    for(unsigned trial=0;trial<10000;trial++) {
        for(unsigned i=0;i<sizeof(fuzz);i++){seed=seed*1664525U+1013904223U;fuzz[i]=seed>>24;}
        if(sm_hid_parse(&m,fuzz,trial%sizeof(fuzz)))sm_hid_input(&m,fuzz,sizeof(fuzz),&pad);
    }
    puts("HID axes/buttons/hat/report IDs, truncation, invalid descriptors and wire sizes OK");
}
