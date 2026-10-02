/* SPDX-License-Identifier: MIT
 * Independent, deliberately narrow FuSa-style scale experiment. No original
 * FuSa code patches/offsets/timers. SDK GE size switch and existing DVE module.
 */
#include <pspkernel.h>
#include <pspctrl.h>
#include <pspinit.h>
#include <pspsdk.h>
#include <pspdisplay.h>
#include <pspge.h>
#include <psppower.h>
#include <pspsysmem_kernel.h>
#include <psploadcore.h>
#include <systemctrl.h>
#include <stdio.h>
#include <string.h>
#include "scale.h"
#include "../psp-overclock/power_callback_slot.h"
PSP_MODULE_INFO("FuSaFullscreenTest",0x1006,0,1);
PSP_NO_CREATE_MAIN_THREAD();
typedef int (*Present)(const void *,int,int,int);
static Present present;
static int (*set_internal)(int,void *,int,int,int);
/* Unlike the public getter, the internal getter RETURNS sync via a pointer.
 * Do not copy FuSa's literal 1 here: it can become a write to address 1. */
static int (*get_internal)(int,void **,int *,int *,int *);
static int (*edram_size)(int);
static unsigned (*get_edram_size)(void);
static unsigned game_edram_size(void){return 0x200000;}
static int (*dve_mode)(int,int,int,int,int,int,int);
static int (*cable_type)(void);
static SceUID worker=-1,callback=-1;
static int power_slot=-1;
static volatile int running,active,cancelled,hooked,users,suspended;
static volatile unsigned frame_addr,frame_stride,frame_format,frames;
static void *saved_overlay;
static int saved_stride,saved_format,old_mode,old_w,old_h,expanded;
static int attempted;
static void record(const char *event,int result)
{
    unsigned long long now=sceKernelGetSystemTimeWide();
    char text[256];int n=snprintf(text,sizeof(text),"%u.%06u %s rc=%08X active=%d frames=%u src=%08X stride=%u fmt=%u\n",
        (unsigned)(now/1000000),(unsigned)(now%1000000),event,result,active,frames,frame_addr,frame_stride,frame_format);
    if(n<0)return;
    if(n>=(int)sizeof(text))n=sizeof(text)-1;
    int fd=sceIoOpen("ms0:/SEPLUGINS/FuSaFullscreenTest/test.log",PSP_O_WRONLY|PSP_O_CREAT|PSP_O_APPEND,0666);
    if(fd>=0){sceIoWrite(fd,text,n);sceIoClose(fd);}
}
/* GE completion alone is racy: a game thread could enqueue another list
 * between the check and the register change. Exclude scheduling/interrupts
 * only for this nonblocking check+switch; never wait with either disabled. */
static int change_edram(int bytes)
{
    record("VRAM transition requested",bytes);
    SceInt64 deadline=sceKernelGetSystemTimeWide()+100000;
    while(!suspended&&sceKernelGetSystemTimeWide()<deadline) {
        int dispatch=sceKernelSuspendDispatchThread();
        if(dispatch<0)return dispatch;
        int intr=sceKernelCpuSuspendIntr();
        int state=sceGeDrawSync(1);
        int rc=state==PSP_GE_LIST_DONE?edram_size(bytes):-1;
        sceKernelCpuResumeIntr(intr);
        sceKernelResumeDispatchThread(dispatch);
        if(state==PSP_GE_LIST_DONE){record("VRAM transition result",rc);return rc;}
        if(state<0){record("GE idle query failed",state);return state;}
        sceKernelDelayThreadCB(1000);
    }
    record("GE busy/suspended; no VRAM register write",-1);return -1;
}
static int capture(const void *base,int stride,int format,int sync)
{
    int intr=sceKernelCpuSuspendIntr();users++;int scaling=active;sceKernelCpuResumeIntr(intr);
    int result;
    if(scaling) {
        if((sync==0||sync==1)&&fs_source_valid((uintptr_t)base,stride,format)) {
            intr=sceKernelCpuSuspendIntr();frame_addr=(unsigned)base;frame_stride=stride;frame_format=format;frames++;
            sceKernelCpuResumeIntr(intr);result=0;
        } else {cancelled=1;result=-1;}
    } else result=present(base,stride,format,sync);
    intr=sceKernelCpuSuspendIntr();users--;sceKernelCpuResumeIntr(intr);return result;
}
static int power_event(int unknown,int flags,void *arg)
{
    (void)unknown;(void)arg;
    if(flags&(PSP_POWER_CB_SUSPENDING|PSP_POWER_CB_STANDBY)){suspended=1;cancelled=1;}
    if(flags&PSP_POWER_CB_RESUME_COMPLETE){suspended=0;cancelled=1;}
    return 0;
}
static int restore(void)
{
    if(!expanded&&!hooked)return 0;
    if(suspended)return -1; /* No hardware changes until resume. */
    /* Keep capture enabled while restoring mode/layers so game submissions
     * cannot race mixed width/stride transitions. No blocking in the hook. */
    int rc=dve_mode(0,old_mode,old_w,old_h,1,15,0);
    int overlay_rc=set_internal(0,saved_overlay,saved_stride,saved_format,1);
    int frame_rc=present((void *)frame_addr,frame_stride,frame_format,1);
    sceKernelDelayThread(50000);
    /* Do not shrink VRAM if a failed restoration might still scan its upper half. */
    if(rc<0||overlay_rc<0||frame_rc<0){cancelled=1;record("restore failed; retain expanded VRAM",rc<0?rc:overlay_rc<0?overlay_rc:frame_rc);return -1;}
    if(expanded){rc=change_edram(0x200000);if(rc<0){cancelled=1;return rc;}expanded=0;}
    int intr=sceKernelCpuSuspendIntr();active=0;
    if(hooked){sctrlHENPatchSyscall((void *)capture,(void *)present);hooked=0;}
    sceKernelCpuResumeIntr(intr);
    intr=sceKernelCpuSuspendIntr();
    sctrlHENPatchSyscall((void *)game_edram_size,(void *)get_edram_size);
    sceKernelCpuResumeIntr(intr);
    record("restored original TV mode",rc);return rc;
}
static int start_scale(void)
{
    void *src=NULL;int stride=0,fmt=-1;
    if(suspended||expanded||hooked)return -1;
    if(attempted){record("one activation per launch in regression build",-1);return -1;}
    if(cable_type()!=2){record("component cable required",-1);return -1;}
    if(sceDisplayGetMode(&old_mode,&old_w,&old_h)<0||old_mode!=0x2d2||old_w!=480||old_h!=272){record("start in Sony game 480p TV mode",-1);return -1;}
    if(sceGeEdramGetSize()!=0x200000){record("VRAM already expanded; refuse ownership",-1);return -1;}
    if(sceDisplayGetFrameBuf(&src,&stride,&fmt,1)<0||!fs_source_valid((uintptr_t)src,stride,fmt)){record("requires 16-bit lower-VRAM game frame",-1);return -1;}
    int saved_sync=0;
    if(get_internal(0,&saved_overlay,&saved_stride,&saved_format,&saved_sync)<0){record("cannot save primary layer",-1);return -1;}
    record("saved primary layer address",(int)saved_overlay);
    record("saved primary layer stride",saved_stride);
    record("saved primary layer format",saved_format);
    frame_addr=(unsigned)src;frame_stride=stride;frame_format=fmt;frames=0;cancelled=0;
    attempted=1;
    int rc=change_edram(0x400000);record("expand VRAM",rc);if(rc<0)return rc;expanded=1;
    int intr=sceKernelCpuSuspendIntr();active=1;
    sctrlHENPatchSyscall((void *)get_edram_size,(void *)game_edram_size);
    sctrlHENPatchSyscall((void *)present,(void *)capture);hooked=1;sceKernelCpuResumeIntr(intr);
    memset((void *)0x44200000,0,2*768*480*2);
    fs_scale16((void *)0x44200000,(void *)((frame_addr&0x1fffffffU)|0x40000000U),frame_stride);
    rc=dve_mode(0,0x1d2,720,480,1,15,0);record("720x480 output",rc);
    if(rc>=0)rc=set_internal(0,(void *)0x04200000,768,fmt,1);
    if(rc>=0)rc=present((void *)0x04200000,768,fmt,1);
    record("first fullscreen frame",rc);
    if(rc<0){restore();return rc;}
    return 0;
}
static int setup(void)
{
    /* Avoid chaining unknown display hooks or competing TV/VRAM owners in this
     * first isolated test. No settings in those plugins are modified. */
    if(sceKernelFindModuleByName("StreamerOC")||sceKernelFindModuleByName("PSPConsolizerUSB")||
       sceKernelFindModuleByName("PSPConsolizer")||sceKernelFindModuleByName("PSPStreamer"))return -2;
    present=(void *)sctrlHENFindFunction("sceDisplay_Service","sceDisplay",0x289D82FE);
    set_internal=(void *)sctrlHENFindFunction("sceDisplay_Service","sceDisplay_driver",0x63E22A26);
    get_internal=(void *)sctrlHENFindFunction("sceDisplay_Service","sceDisplay_driver",0x5B5AEFAD);
    edram_size=(void *)sctrlHENFindFunction("sceGE_Manager","sceGe_driver",0x5BAA5439);
    get_edram_size=(void *)sctrlHENFindFunction("sceGE_Manager","sceGe_driver",0x1F6752AD);
    if(!sceKernelFindModuleByName("pspDveManager_Module")) {
        int id=sceKernelLoadModule("ms0:/SEPLUGINS/FuSaFullscreenTest/dvemgr.prx",0,NULL),status;
        if(id<0)return id;
        int rc=sceKernelStartModule(id,0,NULL,&status,NULL);if(rc<0)return rc;
    }
    dve_mode=(void *)sctrlHENFindFunction("pspDveManager_Module","pspDveManager_driver",0xF9C86C73);
    cable_type=(void *)sctrlHENFindFunction("pspDveManager_Module","pspDveManager_driver",0x2ACFCB6D);
    return present&&set_internal&&get_internal&&edram_size&&get_edram_size&&dve_mode&&cable_type?0:-3;
}
static int work(SceSize size,void *args)
{
    (void)size;(void)args;
    record("FuSaFullscreenTest 0.3 worker-only/GE-idle",sceKernelDevkitVersion());
    for(int i=0;i<100&&running;i++)sceKernelDelayThreadCB(100000);
    if(!running)return 0;
    int rc=setup();record("setup (-2: competing plugins)",rc);if(rc<0)return 0;
    callback=sceKernelCreateCallback("fullscreen power",power_event,NULL);
    int automatic,last;
    if(callback>=0)power_slot=oc_register_power_callback(callback,&automatic,&last);
    if(power_slot<0){record("no power callback; refuse test",power_slot);
        if(callback>=0){sceKernelDeleteCallback(callback);callback=-1;}return 0;}
    unsigned long long until=0,next=0,started_at=0,retry_at=0;unsigned previous=0;int back=1;
    while(running) {
        SceCtrlData pad={0};sceCtrlPeekBufferPositive(&pad,1);
        unsigned chord=PSP_CTRL_NOTE|PSP_CTRL_RTRIGGER;
        unsigned long long now=sceKernelGetSystemTimeWide();
        if(active && !frames && now>started_at+2000000ULL)cancelled=1;
        if(active&&!suspended&&now>=retry_at&&(cancelled||now>=until||(pad.Buttons&(PSP_CTRL_HOME|PSP_CTRL_SCREEN)))) {
            record("automatic/safety stop",cancelled);restore();retry_at=now+1000000;
        }
        if(!suspended && (pad.Buttons&chord)==chord && (previous&chord)!=chord) {
            if(active)restore();
            else if(start_scale()==0){started_at=now;until=now+30000000ULL;next=0;retry_at=0;back=1;}
        }
        previous=pad.Buttons;
        if(active&&!cancelled&&!suspended&&now>=next) {
            unsigned src,stride,format;int intr=sceKernelCpuSuspendIntr();
            src=frame_addr;stride=frame_stride;format=frame_format;sceKernelCpuResumeIntr(intr);
            int mode,w,h;
            if(sceDisplayGetMode(&mode,&w,&h)<0||mode!=0x1d2||w!=720||h!=480||
               sceGeEdramGetSize()!=0x400000){cancelled=1;continue;}
            unsigned dest=0x04200000+back*(768*480*2);
            fs_scale16((void *)(dest|0x40000000U),(void *)((src&0x1fffffffU)|0x40000000U),stride);
            rc=set_internal(0,(void *)dest,768,format,1);
            if(rc>=0)rc=present((void *)dest,768,format,1);
            if(rc<0){record("presentation failed",rc);cancelled=1;}
            back^=1;next=sceKernelGetSystemTimeWide()+83333; /* <=12 Hz preview */
        }
        sceKernelDelayThreadCB(20000);
    }
    restore();
    if(power_slot>=0)scePowerUnregisterCallback(power_slot);
    if(callback>=0)sceKernelDeleteCallback(callback);
    return 0;
}
int module_start(SceSize size,void *args)
{
    (void)size;(void)args;
    unsigned fw=sceKernelDevkitVersion();
    if(sceKernelInitKeyConfig()!=PSP_INIT_KEYCONFIG_GAME||sceKernelGetModel()!=2||
       (fw!=0x06060010&&fw!=0x06060110))return 0;
    running=1;worker=sceKernelCreateThread("fullscreen test",work,0x38,8192,0,NULL);
    if(worker<0)return worker;
    int rc=sceKernelStartThread(worker,0,NULL);
    if(rc<0){running=0;sceKernelDeleteThread(worker);worker=-1;}
    return rc;
}
int module_stop(SceSize size,void *args)
{
    (void)size;(void)args;running=0;
    if(worker>=0){SceUInt wait=2000000;if(sceKernelWaitThreadEnd(worker,&wait)<0)return -1;}
    if(hooked||active||users||expanded)return -1; /* Never unload live hooks. */
    if(worker>=0){sceKernelDeleteThread(worker);worker=-1;}return 0;
}
