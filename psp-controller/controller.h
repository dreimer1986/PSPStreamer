/* SPDX-License-Identifier: GPL-2.0-or-later
 * Resident controller service, compiled into the SAME USB bridge, not a
 * second USB owner. Sony emulation API semantics/NIDs checked against uOFW:
 * https://github.com/uofw/uofw/blob/master/src/kd/ctrl/ctrl.c
 * Optional POPS serial interception is separate from Sony input injection.
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
#include "report_config.h"
#include "home_button.h"
#include "../psp-overclock/title_rules_io.h"

static int (*emulate_buttons)(unsigned char,unsigned,unsigned,unsigned);
static int (*emulate_analog)(unsigned char,unsigned char,unsigned char,unsigned);
static SceUID controller_thread=-1,controller_callback=-1;
static volatile int controller_running,controller_suspended;
static int controller_report=1;
static void controller_log(const char *text,int rc);
#include "overlay.h"
static int controller_enabled=1,controller_vsh=1,controller_pops=1,controller_disabled;
static int controller_home=1,controller_usb_paused;
static char excludes[8][96],allows[8][96];
static unsigned exclude_count,allow_count;

static void controller_log(const char *text,int rc) {
    if(!controller_report)return;
    unsigned long long now=sceKernelGetSystemTimeWide();
    char line[160];int n=snprintf(line,sizeof(line),"%u.%06u %s: %08X\n",(unsigned)(now/1000000),(unsigned)(now%1000000),text,(unsigned)rc);
    if(n<0)return;
    if(n>=(int)sizeof(line))n=sizeof(line)-1;
    int f=sceIoOpen("ms0:/SEPLUGINS/PSPConsolizer/last.log",PSP_O_WRONLY|PSP_O_CREAT|PSP_O_APPEND,0666);
    if(f>=0){sceIoWrite(f,line,n);sceIoClose(f);}
    if(sceKernelInitKeyConfig()==PSP_INIT_KEYCONFIG_POPS) {
        f=sceIoOpen("ms0:/SEPLUGINS/PSPConsolizer/last-pops.log",PSP_O_WRONLY|PSP_O_CREAT|PSP_O_APPEND,0666);
        if(f>=0){sceIoWrite(f,line,n);sceIoClose(f);}
    }
}
#include "tvout.h"
#include "pops_rumble.h"
#include "health_rumble_psp.h"
#include "audio_probe.h"
#include "audio_mirror.h"
#include "startup_diagnostic.h"
#include "xmb_probe.h"
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
    char buffer[2048];int f=sceIoOpen("ms0:/SEPLUGINS/PSPConsolizer/PSPConsolizer.ini",PSP_O_RDONLY,0);
    if(f<0)return;
    int n=sceIoRead(f,buffer,sizeof(buffer)-1);sceIoClose(f);if(n<=0)return;buffer[n]=0;
    controller_report=consolizer_report_setting(buffer);
    char *save=NULL;
    for(char *line=strtok_r(buffer,"\r\n",&save);line;line=strtok_r(NULL,"\r\n",&save)) {
        while(*line==' ' || *line=='\t')line++;
        size_t len=strlen(line);while(len && (line[len-1]==' ' || line[len-1]=='\t'))line[--len]=0;
        if(!strncmp(line,"enabled=",8))controller_enabled=!strcmp(line+8,"1");
        else if(!strncmp(line,"xmb_probe=",10))xmb_probe_enabled=!strcmp(line+10,"1");
        else if(!strncmp(line,"audio_probe=",12))audio_probe_enabled=!strcmp(line+12,"1");
        else if(!strncmp(line,"audio_mirror=",13))audio_mirror_enabled=!strcmp(line+13,"1");
        else if(!strncmp(line,"home_combo=",11))controller_home=!strcmp(line+11,"1");
        else if(!strncmp(line,"vsh=",4))controller_vsh=!strcmp(line+4,"1");
        else if(!strncmp(line,"pops=",5))controller_pops=!strcmp(line+5,"1");
        else if(!strncmp(line,"pops_rumble=",12))pops_rumble_enabled=!strcmp(line+12,"1");
        else if(!strncmp(line,"overlay=",8))pad_overlay_enabled=!strcmp(line+8,"2")?2:!strcmp(line+8,"1");
        else if(!strncmp(line,"overlay_always=",15))pad_overlay_always=!strcmp(line+15,"1");
        else if(!strncmp(line,"tvout=",6))controller_tvout=!strcmp(line+6,"2")?2:!strcmp(line+6,"1");
        else if(!strncmp(line,"metadata=",9))pad_metadata_enabled=!strcmp(line+9,"1");
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
    static const TitleRuleKey rule_keys[]={ {"enabled",0,1},{"home_combo",0,1},
        {"overlay",0,2},{"overlay_always",0,1},{"tvout",0,2},{"metadata",0,1},{"pops_rumble",0,1} };
    int rule_values[]={controller_enabled,controller_home,pad_overlay_enabled,
        pad_overlay_always,controller_tvout,pad_metadata_enabled,pops_rumble_enabled};
    int rule_result=title_rules_load("ms0:/SEPLUGINS/PSPConsolizer/PSPConsolizer-rules.ini",
        sceKernelInitFileName(),rule_keys,7,rule_values);
    controller_log("title/path rule section (negative = invalid)",rule_result);
    SceGameInfo *game_info=sceKernelGetGameInfo();
    if(game_info){char title[40];snprintf(title,sizeof(title),"title ID: %.16s",game_info->title_id);controller_log(title,0);}
    if(rule_result<0)controller_enabled=0;
    else {controller_enabled=rule_values[0];controller_home=rule_values[1];
        pad_overlay_enabled=rule_values[2];pad_overlay_always=rule_values[3];
        controller_tvout=rule_values[4];pad_metadata_enabled=rule_values[5];pops_rumble_enabled=rule_values[6];}
    emulate_buttons=(void *)sctrlHENFindFunction("sceController_Service","sceCtrl_driver",0x5130DAE3);
    emulate_analog=(void *)sctrlHENFindFunction("sceController_Service","sceCtrl_driver",0xDB76878D);
    if(!emulate_buttons || !emulate_analog){controller_log("Sony emulation API unavailable",SM_INVALID);return 0;}
    controller_callback=sceKernelCreateCallback("SM pad power",controller_power,NULL);
    int automatic=0,last=0;
    int power=controller_callback<0?controller_callback:oc_register_power_callback(controller_callback,&automatic,&last);
    if(power<0){controller_log("power callback unavailable (disabled)",power);goto done;}
    int allowed=controller_enabled && controller_context(),last_state=-1;
    if(allowed)audio_mirror_start();
    health_load();
    pad_rumble_enabled=allowed && ((pops_rumble_enabled && sceKernelInitKeyConfig()==PSP_INIT_KEYCONFIG_POPS) || health_enabled);
    controller_log("Consolizer on-demand OSD: context",sceKernelInitKeyConfig());
    controller_log("kernel free bytes before OSD",sceKernelPartitionTotalFreeMemSize(1));
    controller_log("kernel largest block before OSD",sceKernelPartitionMaxFreeMemSize(1));
    controller_log("configured overlay mode",pad_overlay_enabled);
    controller_log("configured TV policy",controller_tvout);
    controller_log("metadata capability",pad_metadata_enabled);
    controller_log("POPS serial capture / EP0 rumble option",pops_rumble_enabled);
    pad_overlay_start();
    xmb_probe_start();
    controller_log("kernel free bytes after OSD",sceKernelPartitionTotalFreeMemSize(1));
    controller_log("kernel largest block after OSD",sceKernelPartitionMaxFreeMemSize(1));
    controller_log("OSD backup bytes",oc_hook_buffer_count*sizeof(OcOverlay));
    unsigned long long next_start=0,next_context=0,escape_since=0;
    int in_streamer=1,usb_error=0;
    int vsh=sceKernelInitKeyConfig()==PSP_INIT_KEYCONFIG_VSH;
    int last_usb=-1,last_driver=-1,last_attach=-1;
    unsigned long long usb_hold_since=0;
    int usb_hold_active=0,usb_hold_fired=0;
    PadHome home={0};
    controller_log("resident service ready",allowed);
    const char *launch_path=sceKernelInitFileName();
    if(launch_path)controller_log(launch_path,0);
    unsigned long long diagnostic_last=0,diagnostic_next=0,diagnostic_gap=0;
    unsigned diagnostic_loops=0;int injection_rc=0;
    while(controller_running) {
        unsigned long long now=sceKernelGetSystemTimeWide();
        pops_rumble_update(now,allowed && !controller_disabled && !controller_suspended);
        if(controller_report){
            if(diagnostic_last && now>=diagnostic_last && now-diagnostic_last>diagnostic_gap)
                diagnostic_gap=now-diagnostic_last;
            diagnostic_last=now;diagnostic_loops++;
        }
        if(now>=next_context) {
            in_streamer=sceKernelFindModuleByName("PSPStreamer")!=NULL;
            next_context=now+100000;
        }
        audio_probe_update(now,allowed && !controller_disabled && !controller_suspended && !in_streamer && !app_owner);
        audio_mirror_allowed=allowed && !controller_disabled && !controller_suspended && !controller_usb_paused && !in_streamer && !app_owner;
        if(allowed)controller_startup_diagnostic(now,in_streamer);
        /* NOTE + VOLUP cannot be synthesized by our 12-button wire mask.
         * Thus only physical PSP buttons can trigger the emergency disable. */
        SceCtrlData physical={0};
        int physical_valid=sceCtrlPeekBufferPositive(&physical,1)>0;
        if(physical_valid &&
           (physical.Buttons&(PSP_CTRL_NOTE|PSP_CTRL_VOLUP))==(PSP_CTRL_NOTE|PSP_CTRL_VOLUP)) {
            if(!escape_since)escape_since=now;
            if(now-escape_since>=2000000 && !controller_disabled) {
                controller_disabled=pad_emergency_stop=1;controller_clear();controller_log("physical emergency disable",0);
            }
        } else escape_since=0;
        /* Explicit VSH ownership hand-off, never automatic PC/ESP guessing.
         * This chord cannot be synthesized by the wire button mask. */
        if(vsh && !app_owner && physical_valid &&
           (physical.Buttons&(PSP_CTRL_NOTE|PSP_CTRL_VOLDOWN))==(PSP_CTRL_NOTE|PSP_CTRL_VOLDOWN)) {
            if(!usb_hold_active){usb_hold_active=1;usb_hold_since=now;}
            if(!usb_hold_fired && now-usb_hold_since>=2000000){
                usb_hold_fired=1;controller_usb_paused=!controller_usb_paused;
                controller_clear();
                controller_log("USB handoff pause",controller_usb_paused);
                next_start=0;
            }
        } else usb_hold_active=usb_hold_fired=0;
        if(controller_usb_paused && started && !app_owner && now>=next_start){
            int rc=devctl(NULL,NULL,SM_DEV_STOP,NULL,0,NULL,0);
            usb_error=rc;controller_log("USB handoff stop",rc);next_start=now+2000000;
        }
        /* Once the app takes ownership it alone controls start/reset/stop.
         * In other apps this service starts only if no other USB function owns
         * the bus. Never deactivate mass storage/camera/etc to take it over. */
        if(allowed && !controller_disabled && !controller_usb_paused && !controller_suspended && !app_owner &&
           !in_streamer && (!started || vsh) && now>=next_start) {
            int was_started=started,usb_state=sceUsbGetState();
            int driver_state=sceUsbGetDrvState(DRIVER);
            if(usb_state!=last_usb || driver_state!=last_driver || attached!=last_attach) {
                controller_log("USB state before maintenance",usb_state);
                controller_log("USB bridge driver state (1 = started)",driver_state);
                controller_log("USB attach speed (0 = absent)",attached);
                last_usb=usb_state;last_driver=driver_state;last_attach=attached;
            }
            int rc=devctl(NULL,NULL,SM_DEV_START,NULL,0,NULL,0);
            usb_error=rc;
            if(rc==0 && (!was_started || !(usb_state&PSP_USB_ACTIVATED) || driver_state!=1))
                controller_log("USB ready; bus owned (0 = borrowed)",sm_bus_owned);
            if(rc<0 && rc!=SM_BUSY)controller_log("USB start",rc);
            next_start=now+2000000;
        }
        SmPad value={.x=128,.y=128};
        int intr=sceKernelCpuSuspendIntr();
        /* Timestamp and snapshot must use the same critical section. An EP0
         * callback may have arrived after the start-of-loop timestamp. */
        unsigned long long sample_now=sceKernelGetSystemTimeWide();
        unsigned long long input_stamp=pad_time;
        if(pad_enabled && pad_time && sample_now>=pad_time && sample_now-pad_time<750000)value=pad_value;
        sceKernelCpuResumeIntr(intr);
        int available=allowed && !controller_disabled && !controller_usb_paused && !controller_suspended && value.connected;
        int active=available && !in_streamer;
        if(health_enabled)health_publish(sample_now,active && !pad_emergency_stop && !app_owner);
        else pops_rumble_publish(sample_now,active && !pad_emergency_stop);
        unsigned buttons=active?value.buttons&0xf3f9U:0;
        unsigned home_button=pad_home_button(&home,&buttons,now,active && controller_home);
        /* PSPStreamer reads ordinary buttons itself, but cannot deliver HOME
         * to the system. Keep this kernel-only escape path alive even when its
         * UI is stuck; no dependence on the application's input loop. */
        home_button|=pad_learned_home(value.buttons,available);
        unsigned screen=controller_tv_button(now,value.connected,in_streamer,allowed && !controller_usb_paused);
        if(active || screen || home_button) {
            /* Four sampling ticks expire even if this thread stalls. A single
             * 100 Hz worker refreshes a cached EP0 value; no USB RPC/polling,
             * per-button threads, heap allocation or network calls. */
            intr=sceKernelCpuSuspendIntr();
            injection_rc=emulate_buttons(3,buttons,buttons|screen|home_button,4);
            if(active && (abs((int)value.x-128)>16 || abs((int)value.y-128)>16))
                emulate_analog(3,value.x,value.y,4);
            else emulate_analog(3,128,128,0);
            sceKernelCpuResumeIntr(intr);
        } else controller_clear();
        int state=controller_suspended?3:in_streamer?2:active?1:0;
        if(state!=last_state){controller_log("state: 0 idle / 1 injected / 2 app-owned / 3 suspended",state);last_state=state;}
        if(controller_report && now>=diagnostic_next){
            unsigned long long age=input_stamp&&sample_now>=input_stamp?sample_now-input_stamp:0xffffffffULL;
            char diagnostic[112];
            snprintf(diagnostic,sizeof(diagnostic),"pad diag loops=%u gap_us=%u age_us=%u seq=%u keys=%08X state=%d",
                diagnostic_loops,(unsigned)(diagnostic_gap>0xffffffffULL?0xffffffffULL:diagnostic_gap),
                (unsigned)(age>0xffffffffULL?0xffffffffULL:age),(unsigned)value.sequence,(unsigned)value.buttons,state);
            controller_log(diagnostic,injection_rc);
            if(health_enabled){
                unsigned events=0,active_rules=0;
                for(int i=0;i<health_profiles.count;i++){events+=health_states[i].events;active_rules+=health_states[i].baseline!=0;}
                snprintf(diagnostic,sizeof(diagnostic),"health rules=%d baselines=%u events=%u",
                    health_profiles.count,active_rules,events);
                controller_log(diagnostic,0);
            }
            diagnostic_loops=0;diagnostic_gap=0;diagnostic_next=now+5000000ULL;
        }
        pad_overlay_update(controller_usb_paused?6:controller_disabled || !allowed?4:controller_suspended?3:!attached?5:value.connected?(in_streamer?2:1):0,usb_error,controller_suspended);
        audio_mirror_log_drain();
        xmb_probe_update(now,allowed && !controller_suspended && !controller_disabled && !controller_usb_paused && !app_owner);
        sceKernelDelayThreadCB(10000);
    }
    scePowerUnregisterCallback(power);
done:
    xmb_probe_stop();
    audio_mirror_stop();
    audio_probe_stop();
    pops_rumble_stop();
    controller_clear();
    if(controller_callback>=0){sceKernelDeleteCallback(controller_callback);controller_callback=-1;}
    return 0;
}
static int controller_start(void) {
    controller_running=1;
    controller_thread=sceKernelCreateThread("PSPConsolizer service",controller_worker,0x30,8192,0,NULL);
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
    if(audio_mirror_pinned)return SM_BUSY;
    return pad_overlay_stop();
}
