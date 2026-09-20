/* Optional plugin cooperation; never set Sony clocks behind another plugin. */
#include "../psp-overclock/control_api.h"
static int music_cpu_mhz,milkdrop_cpu_mhz,video_cpu_mhz,idle_cpu_mhz,screen_idle,power_music;
static int clock_lease,clock_requested=-1,clock_error;
static unsigned long long clock_retry;
static int power_allow_display_idle(int tv){return !tv&&(screen_idle==2||(screen_idle==1&&power_music));}
static int playback_clock_valid(int mhz){return mhz==0||oc_target_valid(mhz);}
static int clock_choice(int current,int direction){
    if(direction>0)return current==0?66:current>=471?0:current+1;
    return current==0?471:current<=66?0:current-1;
}
static void playback_clock(int mhz){
    if(mhz==clock_requested || (!mhz&&!clock_lease))return;
    if(!playback_clock_valid(mhz))return;
    /* The optional plugin starts after six seconds. Retry discovery cheaply,
     * not a blocking sleep in every GUI frame. Failed SETs are not repeated. */
    if(mhz && (unsigned long long)sceKernelGetSystemTimeWide()<clock_retry)return;
    int state=sceIoDevctl(OC_DEVICE,OC_CMD_STATUS,NULL,0,NULL,0);
    if(state<0){clock_error=state;clock_retry=sceKernelGetSystemTimeWide()+2000000ULL;return;}
    clock_retry=0;clock_requested=mhz;
    int r=sceIoDevctl(OC_DEVICE,OC_CMD_SET|(unsigned int)mhz,NULL,0,NULL,0);
    if(r<0){
        /* A rejected MilkDrop profile must not leave the spectrum's low
         * clock active. Return to the INI baseline if we owned an override. */
        if(mhz&&clock_lease){playback_clock(0);clock_requested=mhz;}
        clock_error=r;return;
    }
    clock_lease=!!mhz;
    unsigned long long deadline=sceKernelGetSystemTimeWide()+3000000ULL;
    do {
        state=sceIoDevctl(OC_DEVICE,OC_CMD_STATUS,NULL,0,NULL,0);
        if(state<=0)break;
        sceKernelDelayThread(20000);
    }while((unsigned long long)sceKernelGetSystemTimeWide()<deadline);
    clock_error=state==0?0:-1;
    if(clock_error)sceIoDevctl(OC_DEVICE,OC_CMD_SET,NULL,0,NULL,0);
}
static void playback_clock_release(void){
    if(clock_lease)playback_clock(0);
    clock_lease=0;clock_requested=-1;
    clock_retry=0;
}
static int clock_control_status(void){return sceIoDevctl(OC_DEVICE,OC_CMD_STATUS,NULL,0,NULL,0);}
