/* SPDX-License-Identifier: MIT
 * Independent FuSa-style scaler. System layer priority follows the reference;
 * entry relocation is checked rather than copying legacy firmware offsets.
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
#include "auto_zoom.h"
#include "display_policy.h"
#include "../psp-overclock/power_callback_slot.h"
PSP_MODULE_INFO("FuSaFullscreenTest",0x1006,0,1);
PSP_NO_CREATE_MAIN_THREAD();
typedef int (*Present)(const void *,int,int,int);
static Present present;
static int (*set_internal)(int,void *,int,int,int);
static int (*internal_entry)(int,void *,int,int,int);
static unsigned internal_trampoline[4] __attribute__((aligned(64)));
static unsigned internal_original[2];
static volatile int internal_hooked,internal_users;
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
static int stop_layer;
static int observed_mode,observed_w,observed_h;
static unsigned observed_vram;
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
static int saved_stride,saved_format,old_mode,old_w,old_h,expanded,retiring;
static volatile unsigned overlay_sequence;
static unsigned overlay_copies,overlay_rejected;
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
static int auto_zoom;
static int keep_fullscreen;
static unsigned auto_delay=5;
/* Sony's impose/HOME submits primary layer 0 directly from kernel code, not
 * through the public game syscall. Save its latest source separately. Our
 * worker calls the trampoline and therefore never captures its own output. */
static int capture_internal(int layer,void *base,int stride,int format,int sync)
{
    int intr=sceKernelCpuSuspendIntr();internal_users++;
    int intercept=active&&layer==0;
    if(intercept&&((sync==0||sync==1)&&fs_source_valid((uintptr_t)base,stride,format))) {
        saved_overlay=base;saved_stride=stride;saved_format=format;overlay_sequence++;
    } else if(intercept&&fs_blank_source((uintptr_t)base,stride,format,sync)) {
        saved_overlay=NULL;saved_stride=stride;saved_format=format;overlay_sequence++;
    } else if(intercept){
        if(!cancelled){stop_addr=(unsigned)base;stop_stride=stride;stop_format=format;stop_sync=sync;stop_layer=0;}
        request_stop(STOP_SOURCE,-1);intercept=0;
    }
    sceKernelCpuResumeIntr(intr);
    int rc=intercept?0:set_internal(layer,base,stride,format,sync);
    intr=sceKernelCpuSuspendIntr();internal_users--;sceKernelCpuResumeIntr(intr);
    return rc;
}
static int install_internal_hook(void)
{
    unsigned *entry=(unsigned *)internal_entry;
    int intr=sceKernelCpuSuspendIntr();
    if(internal_hooked||!fs_relocatable(entry[0])||!fs_relocatable(entry[1])||
       (((unsigned)entry^(unsigned)capture_internal)&0xf0000000U)||
       (((unsigned)entry^(unsigned)internal_trampoline)&0xf0000000U)) {
        sceKernelCpuResumeIntr(intr);return -1;
    }
    internal_original[0]=internal_trampoline[0]=entry[0];
    internal_original[1]=internal_trampoline[1]=entry[1];
    internal_trampoline[2]=0x08000000U|(((unsigned)(entry+2)&0x0fffffffU)>>2);
    internal_trampoline[3]=0;
    sceKernelDcacheWritebackInvalidateRange(internal_trampoline,sizeof(internal_trampoline));
    sceKernelIcacheInvalidateRange(internal_trampoline,sizeof(internal_trampoline));
    set_internal=(void *)internal_trampoline;
    entry[0]=0x08000000U|(((unsigned)capture_internal&0x0fffffffU)>>2);entry[1]=0;
    sceKernelDcacheWritebackInvalidateRange(entry,8);sceKernelIcacheInvalidateRange(entry,8);
    internal_hooked=1;sceKernelCpuResumeIntr(intr);return 0;
}
static void remove_internal_hook(void)
{
    int intr=sceKernelCpuSuspendIntr();
    if(internal_hooked) {
        unsigned *entry=(unsigned *)internal_entry;
        entry[0]=internal_original[0];entry[1]=internal_original[1];
        sceKernelDcacheWritebackInvalidateRange(entry,8);sceKernelIcacheInvalidateRange(entry,8);
        internal_hooked=0;
        /* Existing hook callers may still use the trampoline until they exit. */
    }
    sceKernelCpuResumeIntr(intr);
}
/* HOME can suspend the game's submitting thread. Refresh the independently
 * submitted system source without waiting for a game handoff. No global
 * scheduler pause or pixel work inside the kernel presentation hook. */
static int take_overlay_snapshot(unsigned *format)
{
    int intr=sceKernelCpuSuspendIntr();
    void *base=saved_overlay;int stride=saved_stride,fmt=saved_format;
    unsigned sequence=overlay_sequence;
    if(!base){sceKernelCpuResumeIntr(intr);return -1;}
    capture_requested=capture_held=0;
    sceKernelCpuResumeIntr(intr);
    sceKernelSetEventFlag(finished_event,1);
    if(!fs_source_valid((uintptr_t)base,stride,fmt)||sceGeDrawSync(1)!=PSP_GE_LIST_DONE)return 0;
    request_at=sceKernelGetSystemTimeWide();
    void *source=(void *)(((unsigned)base&0x1fffffffU)|0x40000000U);
    if(fmt==3)fs_copy32(snapshot,source,stride);else fs_copy16(snapshot,source,stride);
    int valid=sceGeDrawSync(1)==PSP_GE_LIST_DONE&&sequence==overlay_sequence&&!cancelled&&!suspended;
    if(!valid){overlay_rejected++;return 0;}
    *format=fs_output_format(fmt);overlay_copies++;return 1;
}
static void read_auto_config(void)
{
    char buffer[1025];
    int fd=sceIoOpen("ms0:/SEPLUGINS/FuSaFullscreenTest/FuSaFullscreenTest.ini",PSP_O_RDONLY,0);
    if(fd<0)return;
    int n=sceIoRead(fd,buffer,sizeof(buffer)-1);sceIoClose(fd);
    if(n<=0||n>=1024)return;
    buffer[n]=0;
    char *line=buffer;
    while(*line){char *end=strchr(line,'\n');if(end)*end=0;
        fs_auto_option(line,&auto_zoom,&auto_delay,&keep_fullscreen);if(!end)break;line=end+1;}
}
/* Selected presentation calls yield while the worker copies the submitted
 * front buffer. Only that producer is held, never the whole scheduler/GE.
 * Multi-producer/direct rendering is not assumed safe: observed changes still
 * reject a snapshot. The hook has a bounded wait and performs no pixel work. */
static int take_snapshot(unsigned *format)
{
    int system=take_overlay_snapshot(format);
    if(system>=0)return system;
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
    if(*format==3)fs_copy32(snapshot,(void *)((src&0x1fffffffU)|0x40000000U),stride);
    else fs_copy16(snapshot,(void *)((src&0x1fffffffU)|0x40000000U),stride);
    *format=fs_output_format(*format);
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
        int lower=1;
        if(bytes==0x200000)for(unsigned index=0;index<2;index++) {
            int layer=fs_display_layer(index);
            void *addr=NULL;int stride=0,format=0,sync=0;
            if(get_internal(layer,&addr,&stride,&format,&sync)<0){lower=0;break;}
            unsigned p=(unsigned)addr&0x1fffffffU;
            /* Conservatively include 480 rows: never retire live scanout. */
            if(addr&&stride&&(stride<0||format<0||format>3||
                (p>=0x04000000U&&p<0x04400000U&&
                 p+(unsigned)stride*480U*(format==3?4U:2U)>0x04200000U))){lower=0;break;}
        }
        int rc=state==PSP_GE_LIST_DONE&&lower?edram_size(bytes):-1;
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
            if(can_wait&&running&&!cancelled&&!suspended&&!saved_overlay&&capture_requested&&!capture_held&&users==1) {
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
        } else if(fs_blank_source((uintptr_t)base,stride,format,sync)) {
            /* Impose blanks a layer during HOME transitions. This is not an
             * invalid frame and must not fail the system's display call. */
            result=0;
        } else {
            intr=sceKernelCpuSuspendIntr();
            if(!cancelled){stop_addr=(unsigned)base;stop_stride=stride;stop_format=format;stop_sync=sync;stop_layer=2;}
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
    if(retiring)return -1; /* Sony owns the display already; never replay it. */
    /* Keep capture enabled while restoring mode/layers so game submissions
     * cannot race mixed width/stride transitions. Release capture waiter first. */
    int external=stop_reason==STOP_MODE;
    int rc=external?0:dve_mode(0,old_mode,old_w,old_h,1,15,0);
    int overlay_rc=0,frame_rc=0;
    /* On an external mode change preserve Sony's new layer state, except
     * layers still pointing at our buffers. Do not replay stale TV geometry. */
    for(unsigned index=0;index<2;index++) {
        int layer=fs_display_layer(index);
        void *addr=NULL;int stride=0,format=0,sync=0;
        int query=get_internal(layer,&addr,&stride,&format,&sync);
        unsigned p=(unsigned)addr&0x1fffffffU;
        if(!external || (query>=0&&p>=FS_OUTPUT_BASE&&p<FS_OUTPUT_BASE+2*FS_OUTPUT_BYTES)) {
            if(!layer)overlay_rc=set_internal(0,saved_overlay,saved_stride,saved_format,1);
            else frame_rc=present((void *)frame_addr,frame_stride,frame_format,1);
        } else if(query<0)frame_rc=query;
    }
    sceKernelDelayThread(50000);
    /* Do not shrink VRAM if a failed restoration might still scan its upper half. */
    if(rc<0||overlay_rc<0||frame_rc<0){request_stop(STOP_RESTORE,rc<0?rc:overlay_rc<0?overlay_rc:frame_rc);record("restore failed; retain expanded VRAM",stop_detail);return -1;}
    /* Release capture first, but leave physical VRAM mapped if either Sony
     * layer still references its upper half during a display transition. */
    if(expanded){rc=change_edram(0x200000);if(rc>=0)expanded=0;else retiring=1;}
    int intr=sceKernelCpuSuspendIntr();active=0;
    if(hooked){sctrlHENPatchSyscall((void *)capture,(void *)present);hooked=0;}
    sceKernelCpuResumeIntr(intr);
    remove_internal_hook();
    intr=sceKernelCpuSuspendIntr();
    if(!expanded)sctrlHENPatchSyscall((void *)game_edram_size,(void *)get_edram_size);
    sceKernelCpuResumeIntr(intr);
    record(retiring?"display handed back; VRAM retirement pending":"display handed back",rc);
    record("scaled frames",scaled_frames);
    record("source submissions during scale",changed_during_scale);
    record("scale average us",scaled_frames?(int)(scale_total_us/scaled_frames):0);
    record("scale maximum us",scale_max_us);
    record("RAM snapshots accepted",copied_frames);
    record("RAM snapshots rejected",copy_rejected);
    record("system snapshots accepted",overlay_copies);
    record("system snapshots rejected",overlay_rejected);
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
    if(suspended||expanded||hooked||users||internal_users||internal_hooked)return -1;
    if(cable_type()!=2){record("component cable required",-1);return -1;}
    if(sceDisplayGetMode(&old_mode,&old_w,&old_h)<0||!fs_game_tv_layout(old_mode,old_w,old_h)){
        record("start in Sony game 480p TV mode",-1);record("start mode",old_mode);
        record("start width",old_w);record("start height",old_h);return -1;}
    if(sceGeEdramGetSize()!=0x200000){record("VRAM already expanded; refuse ownership",-1);return -1;}
    if(sceDisplayGetFrameBuf(&src,&stride,&fmt,1)<0||!fs_source_valid((uintptr_t)src,stride,fmt)||
       (((unsigned)src&0x1fffffffU)+(271U*stride+480U)*(fmt==3?4U:2U)>0x04200000U)){
        record("requires nonoverlapping 16/32-bit VRAM frame",-1);record("start source address",(int)src);
        record("start source stride",stride);record("start source format",fmt);return -1;}
    int saved_sync=0;
    if(get_internal(0,&saved_overlay,&saved_stride,&saved_format,&saved_sync)<0){record("cannot save primary layer",-1);return -1;}
    record("saved primary layer address",(int)saved_overlay);
    record("saved primary layer stride",saved_stride);
    record("saved primary layer format",saved_format);
    if(saved_overlay&&!fs_source_valid((uintptr_t)saved_overlay,saved_stride,saved_format))return -1;
    overlay_copies=overlay_rejected=0;overlay_sequence++;
    frame_addr=(unsigned)src;frame_stride=stride;frame_format=fmt;frames=0;cancelled=0;
    stop_reason=STOP_NONE;stop_detail=0;stop_addr=stop_stride=stop_format=stop_sync=0;
    scaled_frames=changed_during_scale=scale_max_us=0;scale_total_us=0;
    copied_frames=copy_rejected=ge_busy=copy_max_us=0;copy_total_us=0;
    handoffs=hold_timeouts=hold_errors=hold_max_us=0;
    hold_total_us=0;held_completed=0;request_at=0;
    sceKernelClearEventFlag(offered_event,0);sceKernelClearEventFlag(finished_event,0);
    capture_requested=capture_held=0; /* Keep ticket monotonic across sessions. */
    int rc=install_internal_hook();record("system display hook",rc);
    if(rc<0){record("system entry instruction 0",((unsigned *)internal_entry)[0]);
        record("system entry instruction 1",((unsigned *)internal_entry)[1]);return rc;}
    rc=change_edram(0x400000);record("expand VRAM",rc);if(rc<0){remove_internal_hook();return rc;}expanded=1;
    int intr=sceKernelCpuSuspendIntr();active=1;
    sctrlHENPatchSyscall((void *)get_edram_size,(void *)game_edram_size);
    sctrlHENPatchSyscall((void *)present,(void *)capture);hooked=1;sceKernelCpuResumeIntr(intr);
    memset((void *)(FS_OUTPUT_BASE|0x40000000U),0,2*FS_OUTPUT_BYTES);
    /* Never scale live game VRAM, including the first frame. The cleared
     * output stays black until the worker accepts a RAM snapshot. */
    rc=dve_mode(0,0x1d2,720,480,1,15,0);record("720x480 output",rc);
    if(rc>=0)rc=set_internal(0,(void *)FS_OUTPUT_BASE,768,fs_output_format(fmt),1);
    if(rc>=0)rc=present((void *)FS_OUTPUT_BASE,768,fs_output_format(fmt),1);
    record("first fullscreen frame",rc);
    if(rc<0){restore();return rc;}
    return 0;
}
static int setup(void)
{
    /* Resolve capabilities, not a plugin/model/firmware allowlist. Overlays
     * and Consolizer may coexist; source and VRAM ownership are checked live. */
    present=(void *)sctrlHENFindFunction("sceDisplay_Service","sceDisplay",0x289D82FE);
    set_internal=(void *)sctrlHENFindFunction("sceDisplay_Service","sceDisplay_driver",0x63E22A26);
    internal_entry=set_internal;
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
    record("FuSaFullscreenTest 0.16 system layer capture",sceKernelDevkitVersion());
    read_auto_config();record("auto zoom enabled",auto_zoom);record("auto zoom delay seconds",auto_delay);
    record("keep fullscreen enabled",keep_fullscreen);
    record("PSP model",sceKernelGetModel());record("execution context",sceKernelInitKeyConfig());
    const char *launch_path=sceKernelInitFileName();
    if(launch_path)record(launch_path,0);
    SceGameInfo *game=sceKernelGetGameInfo();
    if(game){char title[48];snprintf(title,sizeof(title),"title ID: %.16s",game->title_id);record(title,0);}
    for(int i=0;i<100&&running;i++)sceKernelDelayThreadCB(100000);
    if(!running)return 0;
    int rc=setup();record("setup",rc);if(rc<0)return 0;
    callback=sceKernelCreateCallback("fullscreen power",power_event,NULL);
    int automatic,last;
    if(callback>=0)power_slot=oc_register_power_callback(callback,&automatic,&last);
    if(power_slot<0){record("no power callback; refuse test",power_slot);
        if(callback>=0){sceKernelDeleteCallback(callback);callback=-1;}return 0;}
    /* Managed extra shell partition on Slim, when available. No repartition,
     * fixed RAM addresses or theft from the scarce kernel partition. */
    snapshot_block=sceKernelAllocPartitionMemory(11,"fullscreen snapshot",PSP_SMEM_High,480*272*2,NULL);
    record("snapshot extra partition allocation",snapshot_block);
    if(snapshot_block<0)snapshot_block=sceKernelAllocPartitionMemory(2,"fullscreen snapshot",PSP_SMEM_High,480*272*2,NULL);
    if(snapshot_block>=0)snapshot=sceKernelGetBlockHeadAddr(snapshot_block);
    if(!snapshot){record("RAM snapshot allocation failed",snapshot_block);
        record("user RAM free bytes",sceKernelPartitionTotalFreeMemSize(2));
        record("user RAM largest block",sceKernelPartitionMaxFreeMemSize(2));running=0;}
    offered_event=sceKernelCreateEventFlag("fullscreen offered",0,0,NULL);
    finished_event=sceKernelCreateEventFlag("fullscreen finished",0,0,NULL);
    if(offered_event<0||finished_event<0){record("capture event allocation failed",offered_event<0?offered_event:finished_event);running=0;}
    unsigned long long next=0,started_at=0,retry_at=0;unsigned previous=0;int back=1,logged_system=-1;
    FsAutoZoom automatic_zoom={0};unsigned long long auto_poll=0;int zoom_armed=0;
    while(running) {
        SceCtrlData pad={0};sceCtrlPeekBufferPositive(&pad,1);
        unsigned chord=PSP_CTRL_NOTE|PSP_CTRL_RTRIGGER;
        unsigned long long now=sceKernelGetSystemTimeWide();
        if(retiring&&!suspended&&now>=retry_at) {
            retry_at=now+1000000;
            if(change_edram(0x200000)>=0) {
                expanded=retiring=0;
                int intr=sceKernelCpuSuspendIntr();
                sctrlHENPatchSyscall((void *)game_edram_size,(void *)get_edram_size);
                sceKernelCpuResumeIntr(intr);
                record("deferred VRAM retirement complete",0);
            }
        }
        if(active && !keep_fullscreen && !frames && now>started_at+2000000ULL)request_stop(STOP_NO_FRAMES,0);
        if(active&&!suspended&&now>=retry_at&&(cancelled||fs_button_exit(keep_fullscreen,pad.Buttons,PSP_CTRL_HOME|PSP_CTRL_SCREEN))) {
            static const char * const reasons[]={"stop: unspecified","stop: producer wait error",
                "stop: unsupported source submission","stop: suspend","stop: resume",
                "stop: restore failed","stop: VRAM restore failed","stop: no game submissions",
                "stop: output mode/VRAM changed","stop: presentation failed"};
            int was_cancelled=cancelled,reason=stop_reason,detail=stop_detail;
            unsigned rejected_addr=stop_addr,rejected_stride=stop_stride,rejected_format=stop_format,rejected_sync=stop_sync;
            /* Memory-stick logging can take seconds. Hand display ownership
             * back before writing diagnostic lines about the transition. */
            restore();retry_at=sceKernelGetSystemTimeWide()+1000000;
            if(was_cancelled) {
                record(reason>=0&&reason<(int)(sizeof(reasons)/sizeof(reasons[0]))?reasons[reason]:reasons[0],detail);
                if(reason==STOP_SOURCE){record("rejected source address",rejected_addr);record("rejected source stride",rejected_stride);
                    record("rejected source format",rejected_format);record("rejected source sync",rejected_sync);
                    record("rejected internal layer",stop_layer);}
                if(reason==STOP_MODE){record("observed mode",observed_mode);record("observed width",observed_w);
                    record("observed height",observed_h);record("observed VRAM bytes",observed_vram);}
            } else record("stop: HOME/SCREEN button",pad.Buttons);
            if(keep_fullscreen&&zoom_armed){automatic_zoom=(FsAutoZoom){0};auto_poll=retry_at;}
        }
        if(!suspended && (pad.Buttons&chord)==chord && (previous&chord)!=chord) {
            automatic_zoom.handled=1; /* Never immediately undo manual choice. */
            if(active||(keep_fullscreen&&zoom_armed)){zoom_armed=0;record("stop: NOTE+R toggle",0);restore();}
            else {
                if(keep_fullscreen){zoom_armed=1;automatic_zoom=(FsAutoZoom){0};}
                if(start_scale()==0){zoom_armed=1;started_at=sceKernelGetSystemTimeWide();next=0;retry_at=0;back=1;}
            }
        }
        previous=pad.Buttons;
        if(fs_zoom_wanted(auto_zoom,keep_fullscreen,zoom_armed)&&!active&&!suspended&&!expanded&&!hooked&&now>=auto_poll) {
            auto_poll=now+250000ULL;
            int mode=0,w=0,h=0;
            int tv=sceDisplayGetMode(&mode,&w,&h)>=0&&fs_game_tv_layout(mode,w,h)&&cable_type()==2;
            unsigned inhibit=PSP_CTRL_NOTE|PSP_CTRL_RTRIGGER;
            if(!keep_fullscreen)inhibit|=PSP_CTRL_HOME|PSP_CTRL_SCREEN;
            if(pad.Buttons&inhibit)automatic_zoom.timing=0;
            else if(fs_auto_tick(&automatic_zoom,now,tv,auto_delay)) {
                record("automatic TV zoom requested",auto_delay);
                if(keep_fullscreen)zoom_armed=1;
                int start_rc=start_scale();record("automatic TV zoom result",start_rc);
                if(start_rc==0){zoom_armed=1;started_at=sceKernelGetSystemTimeWide();next=0;retry_at=0;back=1;}
                else if(keep_fullscreen&&zoom_armed)automatic_zoom=(FsAutoZoom){0};
            }
        }
        if(active&&!cancelled&&!suspended&&now>=next) {
            unsigned format;
            int mode=0,w=0,h=0;
            int mode_rc=sceDisplayGetMode(&mode,&w,&h);
            unsigned vram=sceGeEdramGetSize();
            if(mode_rc<0||mode!=0x1d2||w!=720||h!=480||vram!=0x400000){
                observed_mode=mode;observed_w=w;observed_h=h;observed_vram=vram;
                request_stop(STOP_MODE,mode_rc);continue;}
            unsigned dest=FS_OUTPUT_BASE+back*FS_OUTPUT_BYTES;
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
            int system=saved_overlay!=NULL;
            if(system!=logged_system) {
                logged_system=system;record("system layer selected",system);
                if(system){record("system source",(int)saved_overlay);record("system stride",saved_stride);record("system format",saved_format);}
            }
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
    if(hooked||active||users||expanded||internal_hooked||internal_users)return -1; /* Never unload live hooks. */
    if(offered_event>=0){sceKernelDeleteEventFlag(offered_event);offered_event=-1;}
    if(finished_event>=0){sceKernelDeleteEventFlag(finished_event);finished_event=-1;}
    if(snapshot_block>=0){sceKernelFreePartitionMemory(snapshot_block);snapshot_block=-1;snapshot=NULL;}
    if(worker>=0){sceKernelDeleteThread(worker);worker=-1;}return 0;
}
