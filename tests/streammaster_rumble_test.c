/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <stdio.h>
#include "../streammaster/rumble.h"
#include "../streammaster/pad_delivery.h"
int main(void) {
    _Static_assert(SM_GAMEPAD_HCI_QUEUE_DEPTH>=2,"rumble admission must be reachable");
    assert(sm_rumble_transport_ready(SM_GAMEPAD_HCI_QUEUE_DEPTH));
    assert(sm_rumble_transport_ready(SM_GAMEPAD_HCI_QUEUE_DEPTH-1));
    assert(sm_rumble_transport_ready(2));
    assert(!sm_rumble_transport_ready(1));assert(!sm_rumble_transport_ready(0));
    assert(!sm_rumble_fresh(1000,0));assert(!sm_rumble_fresh(999,1000));
    assert(sm_rumble_fresh(1000,1000));assert(sm_rumble_fresh(250999,1000));
    assert(!sm_rumble_fresh(251000,1000));assert(!sm_rumble_fresh(UINT64_MAX,1));
    uint8_t frame[8],report[9];
    for(unsigned large=0;large<256;large++)for(unsigned small=0;small<2;small++) {
        sm_rumble_frame(frame,small,large);assert(sm_rumble_valid(frame,8));
        assert(frame[3]==small && frame[4]==large);
        sm_rumble_xbox(report,small,large);
        assert(report[0]==3 && report[1]==15 && !report[2] && !report[3]);
        /* Original, reversed and swapped motor-select masks all select the
         * main motors. No trigger vibration: both trigger values stay zero. */
        assert((report[1]&3)==3 && (report[1]&12)==12);
        assert(report[4]<=100 && report[5]==small*100);
        assert(report[6]==20 && !report[7] && !report[8]);
        if(!large)assert(report[4]==0);
        if(large==255)assert(report[4]==100);
    }
    assert(!sm_rumble_valid(NULL,8));assert(!sm_rumble_valid(frame,7));
    for(unsigned i=0;i<8;i++)if(i!=4){uint8_t old=frame[i];frame[i]=0xff;assert(!sm_rumble_valid(frame,8));frame[i]=old;}
    const uint8_t d[]={0x85,3,0x75,8,0x95,8,0x91,2};
    assert(sm_rumble_xbox_descriptor(0x045e,0x02e0,d,sizeof(d)));
    assert(sm_rumble_xbox_descriptor(0x045e,0x02fd,d,sizeof(d)));
    assert(!sm_rumble_xbox_descriptor(0x2563,0x0526,d,sizeof(d)));
    assert(!sm_rumble_xbox_descriptor(0x045e,0x9999,d,sizeof(d)));
    for(unsigned i=0;i<sizeof(d);i++)assert(!sm_rumble_xbox_descriptor(0x045e,0x02e0,d,i));
    const uint8_t input[]={0x85,3,0x75,8,0x95,8,0x81,2};
    const uint8_t bad[]={0x85,3,0x75,8,0x95,9,0x91,2};
    const uint8_t pop[]={0xb4};
    const uint8_t split[]={0x85,3,0x75,8,0x95,4,0x91,2,0xa4,0x85,4,0x91,2,0xb4,0x91,2};
    assert(!sm_rumble_xbox_descriptor(0x045e,0x02e0,input,sizeof(input)));
    assert(!sm_rumble_xbox_descriptor(0x045e,0x02e0,bad,sizeof(bad)));
    assert(!sm_rumble_xbox_descriptor(0x045e,0x02e0,pop,sizeof(pop)));
    assert(sm_rumble_xbox_descriptor(0x045e,0x02e0,split,sizeof(split)));
    /* Rumble requests reuse the input slot, capped at 50 Hz. Metadata still
     * has space between requests; legacy idle heartbeat stays at 5 Hz. */
    assert(sm_pad_send_kind(20000,0,1,1,0,0,0)==SM_PAD_SEND_INPUT);
    assert(sm_pad_send_kind(10000,0,1,0,0,16,0)==SM_PAD_SEND_NONE);
    assert(sm_pad_send_kind(10000,0,1,1,0,0,0)==SM_PAD_SEND_META);
    assert(sm_pad_send_kind(20000,0,0,0,0,16,0)==SM_PAD_SEND_NONE);
    assert(sm_pad_send_kind(200000,0,0,0,0,16,0)==SM_PAD_SEND_INPUT);
    puts("rumble wire, motor range/duration, identity/descriptor guards and input cadence: OK");
}
