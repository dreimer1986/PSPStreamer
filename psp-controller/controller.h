/* SPDX-License-Identifier: GPL-2.0-or-later
 * Resident controller service, compiled into the SAME USB bridge, not a
 * second USB owner. Sony emulation API semantics/NIDs checked against uOFW:
 * https://github.com/uofw/uofw/blob/master/src/kd/ctrl/ctrl.c
 * No copied function bodies, code patches, syscall hooks or firmware changes.
 */
#include <pspctrl.h>
#include <pspinit.h>
#include <psploadcore.h>
#include <pspsysmem_kernel.h>
#include <psppower.h>
#include <systemctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include "../psp-overclock/power_callback_slot.h"

static int (*emulate_buttons)(unsigned char,unsigned,unsigned,unsigned);
static int (*emulate_analog)(unsigned char,unsigned char,unsigned char,unsigned);
static SceUID controller_thread=-1,controller_callback=-1;
static volatile int controller_running,controller_suspended;
static void controller_log(const char *text,int rc);
#include "overlay.h"
static int controller_enabled=1,controller_vsh=1,controller_pops=1,controller_disabled;
static char excludes[8][96],allows[8][96];
static unsigned exclude_count,allow_count;

static void controller_log(const char *text,int rc) {
    unsigned long long now=sceKernelGetSystemTimeWide();
    char line[160];int n=snprintf(line,sizeof(line),"%u.%06u %s: %08X\n",(unsigned)(now/1000000),(unsigned)(now%1000000),text,(unsigned)rc);
    int f=sceIoOpen("ms0:/SEPLUGINS/StreamMasterPad/last.log",PSP_O_WRONLY|PSP_O_CREAT|PSP_O_APPEND,0666);
    if(f>=0){sceIoWrite(f,line,n);sceIoClose(f);}
}
#include "tvout.h"
static void controller_clear(void) {
    /* Never use an infinite make count. Clear both kernel and user masks
     * explicitly; only our configured slot (3) is touched. */
    if(emulate_buttons)emulate_buttons(3,0,0,0);
    if(emulate_analog)emulate_analog(3,128,128,0);
}
static int controller_power(int unknown,int flags,void *common) {
    (void)unknown;(void)common;
    if(flags&(PSP_POWER_CB_SUSPENDING|PSP_POWER_CB_STANDBY)){controller_suspended=1;pad_time=0;}
    if(flags&PSP_POWER_CB_RESUME_COMPLETE){pad_time=0;controller_suspended=0;}
    return 0;
}
static void controller_config(void) {
    char buffer[2048];int f=sceIoOpen("ms0:/SEPLUGINS/StreamMasterPad/StreamMasterPad.ini",PSP_O_RDONLY,0);
    if(f<0)return;
    int n=sceIoRead(f,buffer,sizeof(buffer)-1);sceIoClose(f);if(n<=0)return;buffer[n]=0;
    char *save=NULL;
    for(char *line=strtok_r(buffer,"\r\n",&save);line;line=strtok_r(NULL,"\r\n",&save)) {
        while(*line==' ' || *line=='\t')line++;
        size_t len=strlen(line);while(len && (line[len-1]==' ' || line[len-1]=='\t'))line[--len]=0;
        if(!strncmp(line,"enabled=",8))controller_enabled=!strcmp(line+8,"1");
        else if(!strncmp(line,"vsh=",4))controller_vsh=!strcmp(line+4,"1");
        else if(!strncmp(line,"pops=",5))controller_pops=!strcmp(line+5,"1");
        else if(!strncmp(line,"overlay=",8))pad_overlay_enabled=!strcmp(line+8,"2")?2:!strcmp(line+8,"1");
        else if(!strncmp(line,"overlay_always=",15))pad_overlay_always=!strcmp(line+15,"1");
        else if(!strncmp(line,"tvout=",6))controller_tvout=!strcmp(line+6,"2")?2:!strcmp(line+6,"1");
        else if(!strncmp(line,"exclude_path=",13) && line[13] && exclude_count<8)
            snprintf(excludes[exclude_count++],96,"%s",line+13);
        else if(!strncmp(line,"allow_path=",11) && line[11] && allow_count<8)
            snprintf(allows[allow_count++],96,"%s",line+11);
    }
}
static int controller_context(void) {
    int key=sceKernelInitKeyConfig();
    if(key!=PSP_INIT_KEYCONFIG_GAME && !(controller_vsh && key==PSP_INIT_KEYCONFIG_VSH) && !(controller_pops && key==PSP_INIT_KEYCONFIG_POPS))return 0;
    const char *path=sceKernelInitFileName();if(!path)path="";
    for(unsigned i=0;i<exclude_count;i++)if(strstr(path,excludes[i]))return 0;
    if(allow_count){for(unsigned i=0;i<allow_count;i++)if(strstr(path,allows[i]))return 1;return 0;}
    return 1;
}
static int controller_worker(SceSize size,void *args) {
    (void)size;(void)args;
    controller_config();
    emulate_buttons=(void *)sctrlHENFindFunction("sceController_Service","sceCtrl_driver",0x5130DAE3);
    emulate_analog=(void *)sctrlHENFindFunction("sceController_Service","sceCtrl_driver",0xDB76878D);
    if(!emulate_buttons || !emulate_analog){controller_log("Sony emulation API unavailable",SM_INVALID);return 0;}
    controller_callback=sceKernelCreateCallback("SM pad power",controller_power,NULL);
    int automatic=0,last=0;
    int power=controller_callback<0?controller_callback:oc_register_power_callback(controller_callback,&automatic,&last);
    if(power<0){controller_log("power callback unavailable (disabled)",power);goto done;}
    int allowed=controller_enabled && controller_context(),last_state=-1;
    controller_log("Consolizer startup-race fix: context",sceKernelInitKeyConfig());
    controller_log("configured overlay mode",pad_overlay_enabled);
    controller_log("configured TV policy",controller_tvout);
    controller_log("overlay initialization",pad_overlay_init());
    unsigned long long next_start=0,next_context=0,escape_since=0;
    int in_streamer=1,usb_error=0;
    controller_log("resident service ready",allowed);
    while(controller_running) {
        unsigned long long now=sceKernelGetSystemTimeWide();
        if(now>=next_context) {
            in_streamer=sceKernelFindModuleByName("PSPStreamer")!=NULL;
            next_context=now+100000;
        }
        /* NOTE + VOLUP cannot be synthesized by our 12-button wire mask.
         * Thus only physical PSP buttons can trigger the emergency disable. */
        SceCtrlData physical;
        if(sceCtrlPeekBufferPositive(&physical,1)>0 &&
           (physical.Buttons&(PSP_CTRL_NOTE|PSP_CTRL_VOLUP))==(PSP_CTRL_NOTE|PSP_CTRL_VOLUP)) {
            if(!escape_since)escape_since=now;
            if(now-escape_since>=2000000 && !controller_disabled) {
                controller_disabled=pad_emergency_stop=1;controller_clear();controller_log("physical emergency disable",0);
            }
        } else escape_since=0;
        /* Once the app takes ownership it alone controls start/reset/stop.
         * In other apps this service starts only if no other USB function owns
         * the bus. Never deactivate mass storage/camera/etc to take it over. */
        if(allowed && !controller_disabled && !controller_suspended && !app_owner && !started && now>=next_start) {
            int rc=devctl(NULL,NULL,SM_DEV_START,NULL,0,NULL,0);
            usb_error=rc;
            if(rc<0 && rc!=SM_BUSY)controller_log("USB start",rc);
            next_start=now+2000000;
        }
        SmPad value={.x=128,.y=128};
        int intr=sceKernelCpuSuspendIntr();
        if(pad_enabled && pad_time && now-pad_time<750000)value=pad_value;
        sceKernelCpuResumeIntr(intr);
        int active=allowed && !controller_disabled && !controller_suspended && !in_streamer && value.connected;
        unsigned screen=controller_tv_button(now,value.connected,in_streamer,allowed);
        if(active || screen) {
            /* Four sampling ticks expire even if this thread stalls. A single
             * 100 Hz worker refreshes a cached EP0 value; no USB RPC/polling,
             * per-button threads, heap allocation or network calls. */
            intr=sceKernelCpuSuspendIntr();
            unsigned buttons=active?value.buttons&0xf3f9U:0;
            emulate_buttons(3,buttons,buttons|screen,4);
            if(active && (abs((int)value.x-128)>16 || abs((int)value.y-128)>16))
                emulate_analog(3,value.x,value.y,4);
            else emulate_analog(3,128,128,0);
            sceKernelCpuResumeIntr(intr);
        } else controller_clear();
        int state=controller_suspended?3:in_streamer?2:active?1:0;
        if(state!=last_state){controller_log("state: 0 idle / 1 injected / 2 app-owned / 3 suspended",state);last_state=state;}
        pad_overlay_update(controller_disabled || !allowed?4:controller_suspended?3:!attached?5:value.connected?(in_streamer?2:1):0,usb_error,controller_suspended);
        sceKernelDelayThreadCB(10000);
    }
    scePowerUnregisterCallback(power);
done:
    controller_clear();
    if(controller_callback>=0){sceKernelDeleteCallback(controller_callback);controller_callback=-1;}
    return 0;
}
static int controller_start(void) {
    controller_running=1;
    controller_thread=sceKernelCreateThread("StreamMasterPad service",controller_worker,0x30,8192,0,NULL);
    if(controller_thread<0)return controller_thread;
    int rc=sceKernelStartThread(controller_thread,0,NULL);
    if(rc<0){sceKernelDeleteThread(controller_thread);controller_thread=-1;controller_running=0;}return rc;
}
static int controller_stop(void) {
    controller_running=0;
    if(controller_thread>=0) {
        SceUInt wait=1000000;int rc=sceKernelWaitThreadEnd(controller_thread,&wait);
        if(rc<0)return rc; /* Never unload live code or forcibly kill an owner. */
        sceKernelDeleteThread(controller_thread);controller_thread=-1;
    }
    return pad_overlay_stop();
}
