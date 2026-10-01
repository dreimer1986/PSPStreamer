/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include "streammaster/gamepad.h"
#include "streammaster/protocol.h"
#include "streammaster/pad_metadata.h"
#include "streammaster/pad_delivery.h"
#include "streammaster/gamepad_options.h"
int main(void) {
    assert(sm_pad_send_kind(30000,0,1,1,0,0,0)==SM_PAD_SEND_INPUT);
    assert(sm_pad_send_kind(30000,0,0,1,0,0,0)==SM_PAD_SEND_META);
    assert(sm_pad_send_kind(30000,0,0,0,0,0,0)==SM_PAD_SEND_NONE);
    assert(sm_pad_send_kind(30000,0,0,1,1,0,0)==SM_PAD_SEND_NONE);
    assert(sm_pad_send_kind(200000,0,0,1,0,0,0)==SM_PAD_SEND_INPUT);
    assert(sm_pad_send_kind(30000,0,1,1,1,0,0)==SM_PAD_SEND_INPUT);
    int input_disabled=0,meta_disabled=0;
    sm_pad_send_failed(1,&input_disabled,&meta_disabled);assert(!input_disabled && meta_disabled);
    sm_pad_send_failed(0,&input_disabled,&meta_disabled);assert(input_disabled && meta_disabled);
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
    SmHidMap m;SmHidPad pad={.x=128,.y=128};
    assert(sm_hid_parse(&m,descriptor,sizeof(descriptor))&&m.count==13);
    uint8_t report[]={0x31,3,0,255,1};
    assert(sm_hid_input(&m,report,sizeof(report),&pad));
    assert(pad.buttons==(0x4000|0x100|0x200|1|8|0x30)&&pad.x==0&&pad.y==255);
    assert(pad.raw_buttons==0x331);
    SmBtOptions options=sm_bt_default_options();assert(sm_bt_options_valid(&options));
    assert(sm_bt_map_buttons(&options,pad.raw_buttons,pad.buttons)==pad.buttons);
    options.button[0]=0x2000;options.button[4]=0x2000;
    assert(sm_bt_map_buttons(&options,0x11,0x40)==0x2040);
    options.button[6]=8;assert(sm_bt_map_buttons(&options,0x40,0)==8);
    options.button[0]=3;assert(!sm_bt_options_valid(&options));
    options.button[0]=0x10000;assert(!sm_bt_options_valid(&options));
    options=sm_bt_default_options();options.reconnect=2;assert(!sm_bt_options_valid(&options));
    SmHidPad before=pad;
    assert(!sm_hid_input(&m,report,2,&pad));assert(!memcmp(&pad,&before,sizeof(pad)));
    uint8_t released[]={0,0,128,127,15};
    assert(sm_hid_input(&m,released,sizeof(released),&pad));assert(!pad.buttons&&!pad.raw_buttons&&pad.x==128&&pad.y==128);
    const uint8_t ids[]={0x05,1,0x85,1,0x09,0x30,0x15,0x81,0x25,127,0x75,8,0x95,1,0x81,2,
                         0x85,2,0x09,0x31,0x81,2};
    assert(sm_hid_parse(&m,ids,sizeof(ids)));
    uint8_t x[]={1,0x81},y[]={2,127},unknown[]={3,0};
    assert(sm_hid_input(&m,x,2,&pad)&&pad.x==0&&pad.y==128);
    assert(sm_hid_input(&m,y,2,&pad)&&pad.x==0&&pad.y==255);
    assert(!sm_hid_input(&m,unknown,2,&pad));
    /* Generic Device Controls Battery Strength: real report only, not a
     * controller-name guess. Missing/invalid values are not empty battery. */
    const uint8_t battery[]={0x05,6,0x09,0x20,0x15,0,0x25,100,0x75,8,0x95,1,0x81,2};
    pad=(SmHidPad){0};assert(!pad.battery_valid);
    assert(sm_hid_parse(&m,battery,sizeof(battery)));
    const uint8_t level[]={73},empty[]={0},full[]={100},invalid[]={255};
    assert(sm_hid_input(&m,level,1,&pad) && pad.battery_valid && pad.battery==73);
    assert(sm_hid_input(&m,empty,1,&pad) && pad.battery_valid && pad.battery==0);
    assert(sm_hid_input(&m,full,1,&pad) && pad.battery_valid && pad.battery==100);
    assert(sm_hid_input(&m,invalid,1,&pad) && !pad.battery_valid);
    SmPadMetaRx rx={0};SmPadMeta meta={.name="Controller",.state=SM_BT_CONNECTED,.valid=1,.battery=73},received={0};
    const uint8_t *wire=(const uint8_t *)&meta;
    for(unsigned i=0;i<16;i++)assert(sm_pad_meta_receive(&rx,i,wire[i*4]|wire[i*4+1]<<8,wire[i*4+2]|wire[i*4+3]<<8,&received)==(i==15));
    assert(!memcmp(&meta,&received,sizeof(meta)));
    memset(&received,0,sizeof(received));
    for(unsigned i=0;i<16;i++)if(i!=4)assert(!sm_pad_meta_receive(&rx,i,wire[i*4]|wire[i*4+1]<<8,wire[i*4+2]|wire[i*4+3]<<8,&received));
    assert(!received.valid); /* interrupted update must not publish */
    assert(!sm_pad_meta_receive(&rx,16,0,0,&received));
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
