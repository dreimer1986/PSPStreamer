#include <assert.h>
#include <stdio.h>
#include "streammaster/gamepad.h"
#include "streammaster/gamepad_learn.h"
#include "streammaster/gamepad_wire.h"
int main(void) {
    _Static_assert(sizeof(SmPad)==32,"USB driver ABI unchanged");
    SmBtProfile p=sm_bt_default_profile();assert(sm_bt_profile_valid(&p));
    SmBtSetup setup={.version=2,.profile=sm_bt_default_profile()};
    sm_bt_setup_assign(&setup,12,2);assert(setup.reserved[0]==2 && !setup.profile.binding[1]);
    sm_bt_setup_assign(&setup,0,2);assert(!setup.reserved[0] && setup.profile.binding[0]==2);
    sm_bt_setup_assign(&setup,12,16);assert(setup.reserved[0]==16);
    assert(sm_pad_unpack_buttons(sm_pad_pack_buttons(0x10009,1))==0x10009);
    assert(sm_pad_pack_buttons(0x10009,0)==9);
    for(unsigned mask=0;mask<65536;mask++)if(!(mask&~0xf3f9U)) {
        assert(sm_pad_unpack_buttons(sm_pad_pack_buttons(mask,1))==mask);
        assert(sm_pad_unpack_buttons(sm_pad_pack_buttons(mask|SM_PAD_HOME,1))==(mask|SM_PAD_HOME));
    }
    SmPad live={.connected=1,.raw_buttons=2};assert(sm_bt_single_source(&live)==2);
    live.raw_buttons=3;assert(!sm_bt_single_source(&live));
    live.raw_buttons=0;live.hat=1;assert(sm_bt_single_source(&live)==17);
    live.hat=3;assert(!sm_bt_single_source(&live));
    sm_bt_assign(&p,0,2);assert(p.binding[0]==2 && p.binding[1]==0);
    assert(sm_bt_profile_buttons(&p,2,0)==0x4000);
    assert(sm_bt_profile_buttons(&p,0,1)==0x10);
    p.axis_x=3;p.axis_y=4;uint8_t values[]={128,128,0,255,0,0};
    assert(sm_bt_profile_axis(&p,values,0x18,0)==255 && sm_bt_profile_axis(&p,values,0x18,1)==0);
    p.invert=3;assert(sm_bt_profile_axis(&p,values,0x18,0)==0 && sm_bt_profile_axis(&p,values,0x18,1)==255);
    assert(sm_bt_profile_axis(&p,values,0,0)==128);
    p.binding[0]=21;assert(!sm_bt_profile_valid(&p));p=sm_bt_default_profile();
    p.axis_y=0;assert(!sm_bt_profile_valid(&p));
    /* Circle on Rx/Ry, with a stationary left stick and two triggers. */
    const uint8_t circle[9][2]={{128,0},{218,38},{255,128},{218,218},{128,255},{38,218},{0,128},{38,38},{128,0}};
    SmBtAxisLearn learn={0};uint8_t x=255,y=255;
    live=(SmPad){.connected=1,.axes_valid=63};memset(live.axes,128,6);live.axes[2]=live.axes[5]=0;
    for(unsigned i=0;i<18;i++) {
        live.sequence++;live.axes[3]=circle[i%9][0];live.axes[4]=circle[i%9][1];
        assert(!sm_bt_learn_axes(&learn,&live,i*100,&x,&y));
    }
    live.axes[3]=live.axes[4]=128;live.sequence++;
    for(unsigned i=0;i<10;i++)assert(sm_bt_learn_axes(&learn,&live,2000+i*20,&x,&y)==(i==9));
    assert(x==3 && y==4);
    /* Jitter or one trigger cannot select a stick. Disconnect resets evidence. */
    live.connected=0;assert(!sm_bt_learn_axes(&learn,&live,9999,&x,&y) && !learn.seen);
    live.connected=1;
    for(unsigned i=0;i<100;i++){live.sequence++;live.axes[3]=125+i%5;live.axes[4]=128;assert(!sm_bt_learn_axes(&learn,&live,9999,&x,&y));}
    /* Parser retains six axes across report IDs. */
    const uint8_t descriptor[]={5,1,0x85,1,0x19,0x30,0x29,0x35,0x15,0,0x26,255,0,0x75,8,0x95,6,0x81,2};
    SmHidMap map;SmHidPad pad={.x=128,.y=128};uint8_t report[]={1,0,255,30,40,50,60};
    assert(sm_hid_parse(&map,descriptor,sizeof(descriptor)) && map.count==6);
    assert(sm_hid_input(&map,report,sizeof(report),&pad) && pad.axes_valid==63);
    assert(!memcmp(pad.axes,report+1,6) && pad.x==0 && pad.y==255);
    puts("PSP-centric mapping, hat capture, six axes, circle/centre detection and ABI OK");
}
