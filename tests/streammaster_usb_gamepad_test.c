/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include "streammaster/usb_gamepad.h"
#include "streammaster/gamepad.h"
int main(void) {
    const uint8_t config[]={9,2,41,0,1,1,0,0x80,50,
        9,4,0,0,2,3,0,0,0,9,0x21,0x10,1,0,1,0x22,97,0,
        7,5,0x81,3,32,0,10,7,5,2,3,32,0,10};
    SmUsbGamepad c;
    assert(sm_usb_gamepad_interface(config,sizeof(config),&c));
    assert(c.interface==0&&c.endpoint==0x81&&c.packet==32&&c.report_bytes==97);
    for(unsigned n=0;n<34;n++)assert(!sm_usb_gamepad_interface(config,n,&c));
    uint8_t bad[sizeof(config)];memcpy(bad,config,sizeof(bad));bad[15]=1;
    assert(!sm_usb_gamepad_interface(bad,sizeof(bad),&c)); /* boot keyboard */
    memcpy(bad,config,sizeof(bad));bad[9]=255;assert(!sm_usb_gamepad_interface(bad,sizeof(bad),&c));
    const uint8_t desc[]={
        0x05,1,0x09,5,0xa1,1,0x85,7,0x09,1,0xa1,0,0x09,0x30,0x09,0x31,
        0x09,0x32,0x09,0x35,0x15,0,0x26,255,0,0x75,8,0x95,4,0x81,2,0xc0,
        0x09,0x39,0x15,0,0x25,7,0x35,0,0x46,0x3b,1,0x65,0x14,0x75,4,0x95,1,
        0x81,0x42,0x75,4,0x95,1,0x81,1,0x05,9,0x19,1,0x29,15,0x15,0,0x25,1,
        0x75,1,0x95,16,0x81,2,0x05,2,0x15,0,0x26,255,0,0x09,0xc4,0x09,0xc5,
        0x95,2,0x75,8,0x81,2,0x75,8,0x95,1,0x81,1,0xc0};
    assert(sizeof(desc)==97 && sm_usb_gamepad_descriptor(desc,sizeof(desc)));
    SmHidMap map;SmHidPad pad={0};assert(sm_hid_parse(&map,desc,sizeof(desc)));
    const uint8_t report[]={7,0,255,128,128,2,1,0,0,0,0};
    assert(sm_hid_input(&map,report,sizeof(report),&pad));
    assert(pad.x==0&&pad.y==255&&pad.raw_buttons==1&&pad.hat==2);
    const uint8_t consumer[]={5,0x0c,9,1,0xa1,1,0xc0};
    assert(!sm_usb_gamepad_descriptor(consumer,sizeof(consumer)));
    puts("USB HID interface, SHANWAN report and bounds: OK");return 0;
}
