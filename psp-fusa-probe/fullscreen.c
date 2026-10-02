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
#include <pspintrman_kernel.h>
#include <pspkerror.h>
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
static SceUID offered_event=-1,finished_event=-1;
#define FS_WAIT_TIMEOUT ((int)SCE_KERNEL_ERROR_WAIT_TIMEOUT)
static int power_slot=-1;
static volatile int running,active,cancelled,hooked,users,suspended;
enum { STOP_NONE,STOP_WAIT,STOP_SOURCE,STOP_SUSPEND,STOP_RESUME,
       STOP_RESTORE,STOP_VRAM_RESTORE,STOP_NO_FRAMES,STOP_MODE,STOP_PRESENT };
static volatile int stop_reason,stop_detail;
static unsigned stop_addr,stop_stride,stop_format,stop_sync;
/* First cause wins; hooks/callbacks only latch data, never perform file I/O. */
static void request_stop(int reason,int detail)
{
    int intr=sceKernelCpuSuspendIntr();
    if(!cancelled){stop_reason=reason;stop_detail=detail;}
    cancelled=1;sceKernelCpuResumeIntr(intr);
    if(finished_event>=0)sceKernelSetEventFlag(finished_event,1);
    if(offered_event>=0)sceKernelSetEventFlag(offered_event,1);
}
static volatile unsigned frame_addr,frame_stride,frame_format,frames;
static void *saved_overlay;
static int saved_stride,saved_format,old_mode,old_w,old_h,expanded;
static unsigned scaled_frames,changed_during_scale,scale_max_us;
static unsigned long long scale_total_us;
static SceUID snapshot_block=-1;
static uint16_t *snapshot;
static unsigned copied_frames,copy_rejected,ge_busy,copy_max_us;
static unsigned long long copy_total_us;
static volatile unsigned capture_requested,capture_held,capture_ticket;
static unsigned held_src,held_stride,held_format,held_sequence;
static unsigned handoffs,hold_timeouts,hold_errors,hold_max_us;
static unsigned long long request_at,hold_total_us;
static unsigned held_completed;
/* Selected presentation calls yield while the worker copies the submitted
 * front buffer. Only that producer is held, never the whole scheduler/GE.
 * Multi-producer/direct rendering is not assumed safe: observed changes still
 * reject a snapshot. The hook has a bounded wait and performs no pixel work. */
static int take_snapshot(unsigned *format)
{
    unsigned src,stride,sequence,ticket;
    int intr=sceKernelCpuSuspendIntr();
    ticket=capture_held;
    if(!ticket){
        if(!capture_requested)request_at=sceKernelGetSystemTimeWide();
        capture_requested=1;sceKernelCpuResumeIntr(intr);return 0;
    }
    src=held_src;stride=held_stride;*format=held_format;sequence=held_sequence;
    sceKernelCpuResumeIntr(intr);
    if(sceGeDrawSync(1)!=PSP_GE_LIST_DONE){ge_busy++;return 0;}
    unsigned long long begin=sceKernelGetSystemTimeWide();
    fs_copy16(snapshot,(void *)((src&0x1fffffffU)|0x40000000U),stride);
    unsigned elapsed=(unsigned)(sceKernelGetSystemTimeWide()-begin);
    copy_total_us+=elapsed;if(elapsed>copy_max_us)copy_max_us=elapsed;
    int idle=sceGeDrawSync(1)==PSP_GE_LIST_DONE;
    intr=sceKernelCpuSuspendIntr();
    int valid=fs_handoff_valid(ticket,capture_held,sequence,frames,idle)&&!cancelled&&!suspended&&running;
    capture_held=0;capture_requested=0;
    sceKernelCpuResumeIntr(intr);
    sceKernelSetEventFlag(finished_event,1);
    if(!valid){copy_rejected++;return 0;}
    copied_frames++;return 1;
}
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
    int can_wait=!sceKernelIsIntrContext();
    int intr=sceKernelCpuSuspendIntr();
    /* Saved interrupt state zero means already disabled (uOFW intr.S). */
    can_wait=can_wait&&intr!=0;
    users++;int scaling=active;sceKernelCpuResumeIntr(intr);
    int result;
    if(scaling) {
        if((sync==0||sync==1)&&fs_source_valid((uintptr_t)base,stride,format)) {
            intr=sceKernelCpuSuspendIntr();frame_addr=(unsigned)base;frame_stride=stride;frame_format=format;frames++;
            unsigned ticket=0;
            if(can_wait&&running&&!cancelled&&!suspended&&capture_requested&&!capture_held&&users==1) {
                sceKernelClearEventFlag(finished_event,0);
                if(++capture_ticket==0)capture_ticket=1;
                ticket=capture_ticket;capture_held=ticket;capture_requested=0;
                held_src=(unsigned)base;held_stride=stride;held_format=format;held_sequence=frames;handoffs++;
            }
            sceKernelCpuResumeIntr(intr);
            if(ticket) {
                unsigned long long begin=sceKernelGetSystemTimeWide();
                sceKernelSetEventFlag(offered_event,1);
                while(capture_held==ticket&&running&&!cancelled&&!suspended) {
                    unsigned long long elapsed=sceKernelGetSystemTimeWide()-begin;
                    if(elapsed>=50000ULL) {
                        intr=sceKernelCpuSuspendIntr();
                        if(capture_held==ticket){hold_timeouts++;capture_held=0;capture_requested=0;}
                        sceKernelCpuResumeIntr(intr);break;
                    }
                    /* Sleep until completion, not every 500 us. The timeout
                     * pointer is ours, never a caller-supplied user pointer. */
                    SceUInt timeout=(SceUInt)(50000ULL-elapsed);
                    unsigned k1=pspSdkSetK1(0);
                    int wait_rc=sceKernelWaitEventFlag(finished_event,1,PSP_EVENT_WAITOR|PSP_EVENT_WAITCLEAR,NULL,&timeout);
                    pspSdkSetK1(k1);
                    if(wait_rc<0&&wait_rc!=FS_WAIT_TIMEOUT){hold_errors++;request_stop(STOP_WAIT,wait_rc);break;}
                }
                intr=sceKernelCpuSuspendIntr();
                if(capture_held==ticket){capture_held=0;capture_requested=0;}
                unsigned elapsed=(unsigned)(sceKernelGetSystemTimeWide()-begin);
                if(elapsed>hold_max_us)hold_max_us=elapsed;
                hold_total_us+=elapsed;held_completed++;
                sceKernelCpuResumeIntr(intr);
            }
            result=0;
        } else {
            intr=sceKernelCpuSuspendIntr();
            if(!cancelled){stop_addr=(unsigned)base;stop_stride=stride;stop_format=format;stop_sync=sync;}
            request_stop(STOP_SOURCE,-1);sceKernelCpuResumeIntr(intr);result=-1;
        }
    } else result=present(base,stride,format,sync);
    intr=sceKernelCpuSuspendIntr();users--;sceKernelCpuResumeIntr(intr);return result;
}
static int power_event(int unknown,int flags,void *arg)
{
    (void)unknown;(void)arg;
    if(flags&(PSP_POWER_CB_SUSPENDING|PSP_POWER_CB_STANDBY)){suspended=1;request_stop(STOP_SUSPEND,flags);}
    if(flags&PSP_POWER_CB_RESUME_COMPLETE){suspended=0;request_stop(STOP_RESUME,flags);}
    return 0;
}
static int restore(void)
{
    int release_intr=sceKernelCpuSuspendIntr();capture_requested=0;capture_held=0;sceKernelCpuResumeIntr(release_intr);
    if(finished_event>=0)sceKernelSetEventFlag(finished_event,1);
    if(!expanded&&!hooked)return 0;
    if(suspended)return -1; /* No hardware changes until resume. */
    /* Keep capture enabled while restoring mode/layers so game submissions
     * cannot race mixed width/stride transitions. Release capture waiter first. */
    int rc=dve_mode(0,old_mode,old_w,old_h,1,15,0);
    int overlay_rc=set_internal(0,saved_overlay,saved_stride,saved_format,1);
    int frame_rc=present((void *)frame_addr,frame_stride,frame_format,1);
    sceKernelDelayThread(50000);
    /* Do not shrink VRAM if a failed restoration might still scan its upper half. */
    if(rc<0||overlay_rc<0||frame_rc<0){request_stop(STOP_RESTORE,rc<0?rc:overlay_rc<0?overlay_rc:frame_rc);record("restore failed; retain expanded VRAM",stop_detail);return -1;}
    if(expanded){rc=change_edram(0x200000);if(rc<0){request_stop(STOP_VRAM_RESTORE,rc);return rc;}expanded=0;}
    int intr=sceKernelCpuSuspendIntr();active=0;
    if(hooked){sctrlHENPatchSyscall((void *)capture,(void *)present);hooked=0;}
    sceKernelCpuResumeIntr(intr);
    intr=sceKernelCpuSuspendIntr();
    sctrlHENPatchSyscall((void *)game_edram_size,(void *)get_edram_size);
    sceKernelCpuResumeIntr(intr);
    record("restored original TV mode",rc);
    record("scaled frames",scaled_frames);
    record("source submissions during scale",changed_during_scale);
    record("scale average us",scaled_frames?(int)(scale_total_us/scaled_frames):0);
    record("scale maximum us",scale_max_us);
    record("RAM snapshots accepted",copied_frames);
    record("RAM snapshots rejected",copy_rejected);
    record("snapshot GE busy polls",ge_busy);
    unsigned copies=copied_frames+copy_rejected;
    record("snapshot copy average us",copies?(int)(copy_total_us/copies):0);
    record("snapshot copy maximum us",copy_max_us);
    record("producer handoffs",handoffs);
    record("producer hold timeouts",hold_timeouts);
    record("producer wait errors",hold_errors);
    record("producer hold maximum us",hold_max_us);
    record("producer hold average us",held_completed?(int)(hold_total_us/held_completed):0);return rc;
}
static int start_scale(void)
{
    void *src=NULL;int stride=0,fmt=-1;
    if(suspended||expanded||hooked||users)return -1;
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
    stop_reason=STOP_NONE;stop_detail=0;stop_addr=stop_stride=stop_format=stop_sync=0;
    scaled_frames=changed_during_scale=scale_max_us=0;scale_total_us=0;
    copied_frames=copy_rejected=ge_busy=copy_max_us=0;copy_total_us=0;
    handoffs=hold_timeouts=hold_errors=hold_max_us=0;
    hold_total_us=0;held_completed=0;request_at=0;
    sceKernelClearEventFlag(offered_event,0);sceKernelClearEventFlag(finished_event,0);
    capture_requested=capture_held=0; /* Keep ticket monotonic across sessions. */
    int rc=change_edram(0x400000);record("expand VRAM",rc);if(rc<0)return rc;expanded=1;
    int intr=sceKernelCpuSuspendIntr();active=1;
    sctrlHENPatchSyscall((void *)get_edram_size,(void *)game_edram_size);
    sctrlHENPatchSyscall((void *)present,(void *)capture);hooked=1;sceKernelCpuResumeIntr(intr);
    memset((void *)0x44200000,0,2*768*480*2);
    /* Never scale live game VRAM, including the first frame. The cleared
     * output stays black until the worker accepts a RAM snapshot. */
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
    record("FuSaFullscreenTest 0.10 30Hz/event handoff",sceKernelDevkitVersion());
    for(int i=0;i<100&&running;i++)sceKernelDelayThreadCB(100000);
    if(!running)return 0;
    int rc=setup();record("setup (-2: competing plugins)",rc);if(rc<0)return 0;
    callback=sceKernelCreateCallback("fullscreen power",power_event,NULL);
    int automatic,last;
    if(callback>=0)power_slot=oc_register_power_callback(callback,&automatic,&last);
    if(power_slot<0){record("no power callback; refuse test",power_slot);
        if(callback>=0){sceKernelDeleteCallback(callback);callback=-1;}return 0;}
    /* User partition, not scarce kernel RAM. Refuse cleanly if unavailable. */
    snapshot_block=sceKernelAllocPartitionMemory(2,"fullscreen snapshot",PSP_SMEM_High,480*272*2,NULL);
    if(snapshot_block>=0)snapshot=sceKernelGetBlockHeadAddr(snapshot_block);
    if(!snapshot){record("RAM snapshot allocation failed",snapshot_block);running=0;}
    offered_event=sceKernelCreateEventFlag("fullscreen offered",0,0,NULL);
    finished_event=sceKernelCreateEventFlag("fullscreen finished",0,0,NULL);
    if(offered_event<0||finished_event<0){record("capture event allocation failed",offered_event<0?offered_event:finished_event);running=0;}
    unsigned long long until=0,next=0,started_at=0,retry_at=0;unsigned previous=0;int back=1;
    while(running) {
        SceCtrlData pad={0};sceCtrlPeekBufferPositive(&pad,1);
        unsigned chord=PSP_CTRL_NOTE|PSP_CTRL_RTRIGGER;
        unsigned long long now=sceKernelGetSystemTimeWide();
        if(active && !frames && now>started_at+2000000ULL)request_stop(STOP_NO_FRAMES,0);
        if(active&&!suspended&&now>=retry_at&&(cancelled||now>=until||(pad.Buttons&(PSP_CTRL_HOME|PSP_CTRL_SCREEN)))) {
            static const char * const reasons[]={"stop: unspecified","stop: producer wait error",
                "stop: unsupported source submission","stop: suspend","stop: resume",
                "stop: restore failed","stop: VRAM restore failed","stop: no game submissions",
                "stop: output mode/VRAM changed","stop: presentation failed"};
            if(cancelled) {
                int reason=stop_reason;
                record(reason>=0&&reason<(int)(sizeof(reasons)/sizeof(reasons[0]))?reasons[reason]:reasons[0],stop_detail);
                if(reason==STOP_SOURCE){record("rejected source address",stop_addr);record("rejected source stride",stop_stride);
                    record("rejected source format",stop_format);record("rejected source sync",stop_sync);}
            } else if(pad.Buttons&(PSP_CTRL_HOME|PSP_CTRL_SCREEN))record("stop: HOME/SCREEN button",pad.Buttons);
            else record("stop: 60 second limit",0);
            restore();retry_at=now+1000000;
        }
        if(!suspended && (pad.Buttons&chord)==chord && (previous&chord)!=chord) {
            if(active){record("stop: NOTE+R toggle",0);restore();}
            else if(start_scale()==0){started_at=sceKernelGetSystemTimeWide();until=started_at+60000000ULL;next=0;retry_at=0;back=1;}
        }
        previous=pad.Buttons;
        if(active&&!cancelled&&!suspended&&now>=next) {
            unsigned format;
            int mode=0,w=0,h=0;
            int mode_rc=sceDisplayGetMode(&mode,&w,&h);
            unsigned vram=sceGeEdramGetSize();
            if(mode_rc<0||mode!=0x1d2||w!=720||h!=480||vram!=0x400000){
                request_stop(STOP_MODE,mode_rc);record("observed mode",mode);record("observed width",w);
                record("observed height",h);record("observed VRAM bytes",vram);continue;}
            unsigned dest=0x04200000+back*(768*480*2);
            if(!take_snapshot(&format)){
                SceUInt timeout=2000;
                int wait_rc=sceKernelWaitEventFlagCB(offered_event,1,PSP_EVENT_WAITOR|PSP_EVENT_WAITCLEAR,NULL,&timeout);
                if(wait_rc<0&&wait_rc!=FS_WAIT_TIMEOUT)request_stop(STOP_WAIT,wait_rc);
                continue;
            }
            unsigned long long cycle=request_at;
            unsigned sequence=frames;
            unsigned long long begin=sceKernelGetSystemTimeWide();
            fs_scale16((void *)(dest|0x40000000U),snapshot,480);
            unsigned elapsed=(unsigned)(sceKernelGetSystemTimeWide()-begin);
            scaled_frames++;scale_total_us+=elapsed;if(elapsed>scale_max_us)scale_max_us=elapsed;
            if(frames!=sequence)changed_during_scale++;
            if(cancelled||suspended)continue;
            rc=set_internal(0,(void *)dest,768,format,1);
            if(rc>=0)rc=present((void *)dest,768,format,1);
            if(rc<0){request_stop(STOP_PRESENT,rc);}
            back^=1;next=fs_next_frame(cycle,sceKernelGetSystemTimeWide());
        }
        /* No 500 Hz polling while merely waiting for the next output slot.
         * Controls/power callbacks still get service at least every 10 ms. */
        unsigned delay=active?2000:20000;
        if(active&&!cancelled){unsigned long long current=sceKernelGetSystemTimeWide();
            if(next>current){unsigned long long left=next-current;delay=left>10000?10000:(unsigned)left;}}
        sceKernelDelayThreadCB(delay);
    }
    if(active)record("stop: module shutdown",0);
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
    if(finished_event>=0)sceKernelSetEventFlag(finished_event,1);
    if(offered_event>=0)sceKernelSetEventFlag(offered_event,1);
    if(worker>=0){SceUInt wait=2000000;if(sceKernelWaitThreadEnd(worker,&wait)<0)return -1;}
    if(hooked||active||users||expanded)return -1; /* Never unload live hooks. */
    if(offered_event>=0){sceKernelDeleteEventFlag(offered_event);offered_event=-1;}
    if(finished_event>=0){sceKernelDeleteEventFlag(finished_event);finished_event=-1;}
    if(snapshot_block>=0){sceKernelFreePartitionMemory(snapshot_block);snapshot_block=-1;snapshot=NULL;}
    if(worker>=0){sceKernelDeleteThread(worker);worker=-1;}return 0;
}
