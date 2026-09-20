#include <assert.h>
#include <stddef.h>
#include "../psp-overclock/control_api.h"
static unsigned long long tick;
static int calls,available=1,pending,stuck,sets,last_set,denied;
static unsigned long long sceKernelGetSystemTimeWide(void){return tick;}
static void sceKernelDelayThread(int us){tick+=us;if(!stuck)pending=0;}
static int sceIoDevctl(const char *device,unsigned cmd,void *in,int il,void *out,int ol){
    assert(device && !in && !il && !out && !ol);calls++;
    if(!available)return -2;
    if(cmd==OC_CMD_STATUS)return pending;
    assert((cmd&OC_CMD_SET_MASK)==OC_CMD_SET);
    if(denied && (cmd&4095))return -3;
    sets++;last_set=cmd&4095;pending=1;return 0;
}
#include "power_policy.h"
int main(void){
    assert(!music_cpu_mhz&&!milkdrop_cpu_mhz&&!video_cpu_mhz&&!idle_cpu_mhz);
    playback_clock(0);assert(!calls);
    assert(clock_control_status()==0);
    assert(playback_clock_valid(0)&&playback_clock_valid(66)&&playback_clock_valid(471));
    assert(!playback_clock_valid(65)&&!playback_clock_valid(472));
    assert(clock_choice(0,1)==66&&clock_choice(66,-1)==0);
    assert(clock_choice(222,1)==223&&clock_choice(222,-1)==221);
    assert(clock_choice(471,1)==0&&clock_choice(0,-1)==471);
    available=0;playback_clock(222);assert(clock_error<0&&!clock_lease);
    int before=calls;playback_clock(222);assert(calls==before);
    available=1;tick+=2000000;playback_clock(222);
    assert(last_set==222&&clock_lease&&!clock_error);
    before=calls;playback_clock(222);assert(calls==before);
    playback_clock(443);assert(last_set==443&&clock_lease);
    playback_clock(0);assert(last_set==0&&!clock_lease);
    playback_clock_release();
    playback_clock(111);denied=1;playback_clock(471);
    assert(clock_error<0&&!clock_lease&&last_set==0);
    denied=0;playback_clock_release();
    denied=1;playback_clock(471);assert(clock_error<0&&!clock_lease);
    before=calls;playback_clock(471);assert(calls==before);denied=0;
    playback_clock_release();
    stuck=1;unsigned long long start=tick;playback_clock(166);
    assert(clock_error<0&&tick-start==3000000&&last_set==0);
    stuck=0;pending=0;playback_clock_release();
    /* Episode/track/seek boundaries retain the request cache: identical
     * profiles send nothing, different renderers request exactly one target. */
    playback_clock(266);before=sets;
    for(int i=0;i<20;i++)playback_clock(266);
    assert(sets==before&&clock_lease&&last_set==266);
    playback_clock(133);assert(sets==before+1&&last_set==133);
    before=sets;playback_clock(133);assert(sets==before);
    idle_cpu_mhz=133;playback_clock_idle();assert(sets==before);
    idle_cpu_mhz=222;playback_clock_idle();assert(sets==before+1&&last_set==222);
    idle_cpu_mhz=0;playback_clock_idle();assert(sets==before+2&&last_set==0&&!clock_lease);
    before=sets;playback_clock_idle();assert(sets==before);
    /* Explicit zero next-profile still releases a previous override. */
    playback_clock(266);before=sets;playback_clock(0);
    assert(sets==before+1&&last_set==0&&!clock_lease);
    assert(!power_allow_display_idle(0));
    screen_idle=1;assert(!power_allow_display_idle(0));
    power_music=1;assert(power_allow_display_idle(0)&&!power_allow_display_idle(1));
    power_music=0;screen_idle=2;assert(power_allow_display_idle(0)&&!power_allow_display_idle(1));
    assert(sets>=5);return 0;
}
