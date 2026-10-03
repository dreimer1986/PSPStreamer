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
#include <pspiofilemgr_kernel.h>
#include <systemctrl.h>
#include <stdio.h>
#include <string.h>
#include "scale.h"
#include "auto_zoom.h"
#include "display_policy.h"
#include "../psp-overclock/power_callback_slot.h"
#define FS_OSD_SERVER 1
#include "../psp-overclock/fullscreen_osd.h"
#include "../psp-overclock/overlay_pixels.h"
PSP_MODULE_INFO("FuSaFullscreen",0x1006,0,26);
PSP_NO_CREATE_MAIN_THREAD();
static int (*set_internal)(int,void *,int,int,int);
static int (*internal_entry)(int,void *,int,int,int);
static unsigned internal_trampoline[4] __attribute__((aligned(64)));
static unsigned internal_original[2];
static volatile int internal_hooked,internal_users;
static int (*mode_entry)(int,int,int),(*set_mode_original)(int,int,int);
static unsigned mode_trampoline[4] __attribute__((aligned(64)));
static unsigned mode_original[2];
static volatile int mode_hooked,mode_users;
static int (*get_mode_original)(int *,int *,int *);
static volatile unsigned mode_queries,mode_query_users,mode_events;
static int query_hooked,logical_mode,logical_width,logical_height;
static int requested_mode,requested_width,requested_height,requested_result,requested_route;
static volatile unsigned long long screen_until;
/* Unlike the public getter, the internal getter RETURNS sync via a pointer.
 * Do not copy FuSa's literal 1 here: it can become a write to address 1. */
static int (*get_internal)(int,void **,int *,int *,int *);
static int (*edram_size)(int);
static unsigned (*get_edram_size)(void);
static unsigned game_edram_size(void){return 0x200000;}
static int (*dve_mode)(int,int,int,int,int,int,int);
static int (*cable_type)(void);
static SceUID worker=-1,callback=-1;
static SceUID offered_event=-1;
#define FS_WAIT_TIMEOUT ((int)SCE_KERNEL_ERROR_WAIT_TIMEOUT)
static int power_slot=-1;
static volatile int running,active,cancelled,suspended,restoring;
static FsOsdText osd_text[2];
static unsigned long long osd_updated[2];
static int osd_registered;
static int osd_devctl(PspIoDrvFileArg *arg,const char *name,unsigned cmd,void *in,int inlen,void *out,int outlen)
{
    (void)arg;(void)name;
    if(pspSdkGetK1()!=0||out||outlen)return -1;
    if(cmd==FS_OSD_STATUS&&!in&&!inlen)return active&&!restoring&&!cancelled&&!suspended&&running;
    if(cmd!=FS_OSD_TEXT||!in||inlen!=sizeof(FsOsdText)||(uintptr_t)in<0x88000000U)return -1;
    FsOsdText text;memcpy(&text,in,sizeof(text));
    if(!fs_osd_message_valid(&text))return -1;
    for(unsigned row=0;row<3;row++)text.lines[row][39]=0;
    int intr=sceKernelCpuSuspendIntr();osd_text[text.slot]=text;
    osd_updated[text.slot]=sceKernelGetSystemTimeWide();sceKernelCpuResumeIntr(intr);return 0;
}
static int osd_init(PspIoDrvArg *arg){(void)arg;return 0;}
static PspIoDrvFuncs osd_functions={.IoInit=osd_init,.IoExit=osd_init,.IoDevctl=osd_devctl};
static PspIoDrv osd_driver={"fusafullscreen",0x10,0x800,"Fullscreen text OSD",&osd_functions};
static void draw_output_osd(unsigned dest,unsigned format)
{
    unsigned long long now=sceKernelGetSystemTimeWide();
    for(unsigned slot=0;slot<2;slot++) {
        int intr=sceKernelCpuSuspendIntr();
        FsOsdText text=osd_text[slot];unsigned long long stamp=osd_updated[slot];
        sceKernelCpuResumeIntr(intr);
        if(text.visible&&stamp&&now>=stamp&&now-stamp<500000ULL)
            oc_osd_draw_fresh((void *)(dest|0x40000000U),768,format,slot?500:8,slot?212:236,text.lines);
    }
}
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
    if(offered_event>=0)sceKernelSetEventFlag(offered_event,1);
}
static volatile unsigned frame_addr,frame_stride,frame_format,frames;
static volatile unsigned source_layout;
static void *saved_overlay;
static void *saved_aux;
static int aux_stride,aux_format;
static int saved_stride,saved_format,old_mode,old_w,old_h,expanded,retiring;
static volatile unsigned overlay_sequence;
static unsigned overlay_copies,overlay_rejected;
static unsigned system_ram_start[4],system_ram_end[4];
static unsigned displayed_base,displayed_format,mode_remaps;
static volatile unsigned game_writers;
static int system_source_valid(uintptr_t base,int stride,int format)
{
    if(fs_source_valid(base,stride,format))return 1;
    for(unsigned i=0;i<4;i++)if(fs_ram_source_valid(base,stride,format,system_ram_start[i],system_ram_end[i]))return 1;
    return 0;
}
static unsigned scaled_frames,changed_during_scale,scale_max_us;
static unsigned long long scale_total_us;
static SceUID snapshot_block=-1;
static uint16_t *snapshot;
static unsigned copied_frames,copy_rejected,ge_busy,copy_max_us;
static unsigned long long copy_total_us;
static unsigned long long request_at,last_snapshot_at;
static unsigned last_snapshot_sequence,timeout_snapshots;
static volatile int capture_busy;
void fs_copy16_vfpu(uint16_t *,const void *,int);
void fs_copy32_vfpu(uint16_t *,const void *,int);
static void copy_source(unsigned base,int stride,int format)
{
    capture_busy=1;
    unsigned physical=base&0x1fffffffU;
    const void *source=(void *)fs_source_alias(base);
    if(physical<0x08000000U) {
        /* VRAM read-only cached alias: discard stale CPU cache, never write
         * old pixels back over GE output. Main-RAM system buffers retain the
         * existing uncached path; do not discard their producer's dirty data. */
        source=(void *)physical;
        sceKernelDcacheInvalidateRange(source,(271U*stride+480U)*(format==3?4U:2U));
    }
    if(format==3)fs_copy32_vfpu(snapshot,source,stride);
    else fs_copy16_vfpu(snapshot,source,stride);
    capture_busy=0;
}
static int auto_zoom;
static int keep_fullscreen;
static unsigned auto_delay=5;
static int experimental_speedboost,wait_hooks;
static volatile unsigned wait_users;
static unsigned wait_calls,wait_timeouts;
static unsigned long long coordinated_us;
static int (*original_wait[4])(void);
/* Coordination belongs at game wait points, NEVER inside the display setter.
 * This is an ARK adaptation, not the commented legacy fake-interrupt code. */
static int coordinated_wait(unsigned index)
{
    int intr=sceKernelCpuSuspendIntr();wait_users++;
    sceKernelCpuResumeIntr(intr);
    /* Sony's wait runs exactly once FIRST. Never add another vblank after
     * capture and never wait for a future 30 Hz output slot. */
    int rc=original_wait[index]();
    if(rc>=0&&active&&!restoring&&!saved_overlay&&!saved_aux&&running&&capture_busy&&sceKernelGetThreadId()!=worker) {
        wait_calls++;
        unsigned long long begin=sceKernelGetSystemTimeWide(),deadline=begin+4000ULL;
        while(active&&!restoring&&!cancelled&&!suspended&&!saved_overlay&&!saved_aux&&running&&
              capture_busy) {
            if((unsigned long long)sceKernelGetSystemTimeWide()>=deadline){wait_timeouts++;break;}
            /* Preserve callback dispatch for the CB entry points. */
            if(index&1)sceKernelDelayThreadCB(250);else sceKernelDelayThread(250);
        }
        coordinated_us+=(unsigned long long)sceKernelGetSystemTimeWide()-begin;
    }
    intr=sceKernelCpuSuspendIntr();wait_users--;sceKernelCpuResumeIntr(intr);
    return rc;
}
static int coordinated_wait_plain(void){return coordinated_wait(0);}
static int coordinated_wait_cb(void){return coordinated_wait(1);}
static int coordinated_wait_start(void){return coordinated_wait(2);}
static int coordinated_wait_start_cb(void){return coordinated_wait(3);}
static void *wait_replacement[4]={coordinated_wait_plain,coordinated_wait_cb,coordinated_wait_start,coordinated_wait_start_cb};
static int install_wait_hooks(void)
{
    const unsigned nids[4]={0x36CDFADE,0x8EB9EC49,0x984C27E7,0x46F186C3};
    if(!experimental_speedboost)return 0;
    for(unsigned i=0;i<4;i++) {
        original_wait[i]=(void *)sctrlHENFindFunction("sceDisplay_Service","sceDisplay",nids[i]);
        if(!original_wait[i])return -1;
    }
    int intr=sceKernelCpuSuspendIntr();
    for(unsigned i=0;i<4;i++)sctrlHENPatchSyscall((void *)original_wait[i],wait_replacement[i]);
    wait_hooks=1;sceKernelCpuResumeIntr(intr);return 0;
}
static void remove_wait_hooks(void)
{
    if(!wait_hooks)return;
    int intr=sceKernelCpuSuspendIntr();
    for(unsigned i=0;i<4;i++)sctrlHENPatchSyscall(wait_replacement[i],(void *)original_wait[i]);
    wait_hooks=0;sceKernelCpuResumeIntr(intr);
}
static int capture_game(void *base,int stride,int format,int sync)
{
    int intr=sceKernelCpuSuspendIntr();game_writers++;sceKernelCpuResumeIntr(intr);
    /* Preserve Sony's actual game-layer state and return value. The public
     * syscall and direct kernel producers both arrive here exactly once. */
    int rc=set_internal(2,base,stride,format,sync);
    intr=sceKernelCpuSuspendIntr();
    if(rc>=0) {
        if(system_source_valid((uintptr_t)base,stride,format)||fs_blank_source((uintptr_t)base,stride,format,sync)) {
            if(frame_stride!=(unsigned)stride||frame_format!=(unsigned)format||
               (!frame_addr)!=fs_blank_source((uintptr_t)base,stride,format,sync))source_layout++;
            frame_addr=fs_blank_source((uintptr_t)base,stride,format,sync)?0:(unsigned)base;
            frame_stride=stride;frame_format=format;frames++;
        } else {
            if(!cancelled){stop_addr=(unsigned)base;stop_stride=stride;stop_format=format;stop_sync=sync;stop_layer=2;}
            request_stop(STOP_SOURCE,-1);
        }
    }
    game_writers--;sceKernelCpuResumeIntr(intr);
    if(rc>=0&&offered_event>=0)sceKernelSetEventFlag(offered_event,1);
    return rc;
}
/* Sony's impose/HOME submits primary layer 0 directly from kernel code, not
 * through the public game syscall. Save its latest source separately. Our
 * worker calls the trampoline and therefore never captures its own output. */
static int capture_internal(int layer,void *base,int stride,int format,int sync)
{
    int intr=sceKernelCpuSuspendIntr();internal_users++;
    /* DVE can itself call this entry during our worker's start/restore. Those
     * are output operations, not a newly submitted Sony menu source. */
    int intercept=fs_layer_route(active&&!restoring,sceKernelGetThreadId()==worker,layer)==FS_SYSTEM;
    if(intercept&&((sync==0||sync==1)&&system_source_valid((uintptr_t)base,stride,format))) {
        if(!saved_overlay||saved_stride!=stride||saved_format!=format)source_layout++;
        saved_overlay=base;saved_stride=stride;saved_format=format;overlay_sequence++;
    } else if(intercept&&fs_blank_source((uintptr_t)base,stride,format,sync)) {
        if(saved_overlay)source_layout++;
        saved_overlay=NULL;saved_stride=stride;saved_format=format;overlay_sequence++;
    } else if(intercept){
        if(!cancelled){stop_addr=(unsigned)base;stop_stride=stride;stop_format=format;stop_sync=sync;stop_layer=0;}
        request_stop(STOP_SOURCE,-1);intercept=0;
    }
    unsigned output_base=displayed_base,output_format=displayed_format;
    sceKernelCpuResumeIntr(intr);
    int game=fs_layer_route(active,sceKernelGetThreadId()==worker,layer)==FS_GAME;
    int rc=intercept?set_internal(0,(void *)output_base,768,output_format,sync):
        game?capture_game(base,stride,format,sync):set_internal(layer,base,stride,format,sync);
    intr=sceKernelCpuSuspendIntr();
    /* CustomHOME uses auxiliary layer 1. Preserve its actual driver call and
     * separately capture its menu source; primary layer 0 remains scanout. */
    if(rc>=0&&fs_layer_route(active&&!restoring,sceKernelGetThreadId()==worker,layer)==FS_AUX) {
        if(system_source_valid((uintptr_t)base,stride,format)||fs_blank_source((uintptr_t)base,stride,format,sync)) {
            saved_aux=fs_blank_source((uintptr_t)base,stride,format,sync)?NULL:base;
            aux_stride=stride;aux_format=format;source_layout++;overlay_sequence++;
        } else {
            if(!cancelled){stop_addr=(unsigned)base;stop_stride=stride;stop_format=format;stop_sync=sync;stop_layer=1;}
            request_stop(STOP_SOURCE,-1);
        }
    }
    internal_users--;sceKernelCpuResumeIntr(intr);
    return rc;
}
static int install_entry(unsigned *entry,void *replacement,unsigned *trampoline,unsigned *original)
{
    int intr=sceKernelCpuSuspendIntr();
    if(!fs_relocatable(entry[0])||!fs_relocatable(entry[1])||
       (((unsigned)entry^(unsigned)replacement)&0xf0000000U)||
       (((unsigned)entry^(unsigned)trampoline)&0xf0000000U)) {
        sceKernelCpuResumeIntr(intr);return -1;
    }
    original[0]=trampoline[0]=entry[0];original[1]=trampoline[1]=entry[1];
    trampoline[2]=0x08000000U|(((unsigned)(entry+2)&0x0fffffffU)>>2);trampoline[3]=0;
    sceKernelDcacheWritebackInvalidateRange(trampoline,16);
    sceKernelIcacheInvalidateRange(trampoline,16);
    entry[0]=0x08000000U|(((unsigned)replacement&0x0fffffffU)>>2);entry[1]=0;
    sceKernelDcacheWritebackInvalidateRange(entry,8);sceKernelIcacheInvalidateRange(entry,8);
    sceKernelCpuResumeIntr(intr);return 0;
}
static int capture_mode(int mode,int width,int height)
{
    int intr=sceKernelCpuSuspendIntr();mode_users++;
    int own=sceKernelGetThreadId()==worker;
    if(active&&!own)source_layout++;
    int lcd_requested=(unsigned long long)sceKernelGetSystemTimeWide()<screen_until;
    int redirect=fs_redirect_mode(active&&!restoring&&!suspended&&!cancelled,own,lcd_requested,mode,width,height);
    int input_mode=mode,input_width=width,input_height=height;
    if(redirect){mode=0x1d2;width=720;height=480;mode_remaps++;}
    sceKernelCpuResumeIntr(intr);
    int rc,route=0;
    if(redirect) {
        /* A logical game reset does not require resetting an already correct
         * physical TV mode. Verify before accepting the idempotent request;
         * do not feed privileged TV arguments back through a user-context
         * call and return INVALID_MODE to a movie's initialization loop. */
        int actual_mode=0,actual_width=0,actual_height=0;
        /* This query writes plugin-owned kernel-stack locals, not user data. */
        unsigned k1=pspSdkSetK1(0);
        rc=sceDisplayGetMode(&actual_mode,&actual_width,&actual_height);
        if(rc>=0&&fs_scaled_mode(actual_mode,actual_width,actual_height)){route=1;rc=0;}
        else if(rc>=0) {
            route=2;
            /* Only our validated, pointer-free fixed output arguments get
             * kernel context. Unredirected user calls retain their checks. */
            rc=set_mode_original(mode,width,height);
        }
        pspSdkSetK1(k1);
    } else rc=set_mode_original(mode,width,height);
    intr=sceKernelCpuSuspendIntr();
    if(active&&!own) {
        requested_mode=input_mode;requested_width=input_width;requested_height=input_height;requested_result=rc;requested_route=route;mode_events++;
        if(rc>=0&&redirect){logical_mode=input_mode;logical_width=input_width;logical_height=input_height;}
    }
    mode_users--;sceKernelCpuResumeIntr(intr);return rc;
}
/* Only user syscalls see logical game geometry. The display driver, worker,
 * DVE and kernel system UI must continue to see actual output geometry. */
static int game_get_mode(int *mode,int *width,int *height)
{
    int intr=sceKernelCpuSuspendIntr();mode_query_users++;sceKernelCpuResumeIntr(intr);
    /* Sony performs its normal user pointer validation before any overwrite. */
    int rc=get_mode_original(mode,width,height);
    intr=sceKernelCpuSuspendIntr();
    if(fs_report_game_mode(active&&!restoring&&!cancelled&&!suspended,sceKernelGetThreadId()==worker,rc)) {
        if(mode)*mode=logical_mode;
        if(width)*width=logical_width;
        if(height)*height=logical_height;
        mode_queries++;
    }
    mode_query_users--;sceKernelCpuResumeIntr(intr);return rc;
}
static int install_internal_hook(void)
{
    int intr=sceKernelCpuSuspendIntr();
    set_internal=(void *)internal_trampoline;set_mode_original=(void *)mode_trampoline;
    int rc=install_entry((unsigned *)internal_entry,(void *)capture_internal,internal_trampoline,internal_original);
    if(rc>=0)internal_hooked=1;
    else set_internal=internal_entry;
    if(rc>=0){rc=install_entry((unsigned *)mode_entry,(void *)capture_mode,mode_trampoline,mode_original);if(rc>=0)mode_hooked=1;}
    sceKernelCpuResumeIntr(intr);return rc;
}
static void remove_internal_hook(void)
{
    int intr=sceKernelCpuSuspendIntr();
    if(query_hooked){sctrlHENPatchSyscall((void *)game_get_mode,(void *)get_mode_original);query_hooked=0;}
    if(mode_hooked){
        unsigned *entry=(unsigned *)mode_entry;entry[0]=mode_original[0];entry[1]=mode_original[1];
        sceKernelDcacheWritebackInvalidateRange(entry,8);sceKernelIcacheInvalidateRange(entry,8);mode_hooked=0;
    }
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
    if(fs_selected_source(base!=NULL,saved_aux!=NULL)==1){base=saved_aux;stride=aux_stride;fmt=aux_format;}
    unsigned layout=source_layout;
    unsigned sequence=overlay_sequence;
    if(!base){sceKernelCpuResumeIntr(intr);return -1;}
    sceKernelCpuResumeIntr(intr);
    if(!system_source_valid((uintptr_t)base,stride,fmt))return 0;
    request_at=sceKernelGetSystemTimeWide();
    copy_source((unsigned)base,stride,fmt);
    int valid=fs_snapshot_layout_valid(layout,source_layout)&&sequence==overlay_sequence&&!cancelled&&!suspended&&running;
    if(!valid){overlay_rejected++;return 0;}
    *format=fs_output_format(fmt);overlay_copies++;return 1;
}
static void read_auto_config(void)
{
    char buffer[1025];
    int fd=sceIoOpen("ms0:/SEPLUGINS/FuSaFullscreen/FuSaFullscreen.ini",PSP_O_RDONLY,0);
    if(fd<0)return;
    int n=sceIoRead(fd,buffer,sizeof(buffer)-1);sceIoClose(fd);
    if(n<=0||n>=1024)return;
    buffer[n]=0;
    char *line=buffer;
    while(*line){char *end=strchr(line,'\n');if(end)*end=0;
        fs_auto_option_ex(line,&auto_zoom,&auto_delay,&keep_fullscreen,&experimental_speedboost);if(!end)break;line=end+1;}
}
/* Event-driven, nonblocking source observation as in FuSa. Never wait inside
 * Sony's internal setter: its caller may already own display synchronization.
 * VFPU/cache-assisted copying is taken just after vblank at reference worker
 * priority. Reject a swap during this short copy, not during later scaling. */
static int take_snapshot(unsigned *format)
{
    int system=take_overlay_snapshot(format);
    if(system>=0)return system;
    unsigned src,stride,sequence;
    int intr=sceKernelCpuSuspendIntr();
    src=frame_addr;stride=frame_stride;*format=frame_format;sequence=frames;
    unsigned layout=source_layout;
    unsigned writers=game_writers;
    sceKernelCpuResumeIntr(intr);
    unsigned long long now=sceKernelGetSystemTimeWide();
    if(writers||!src||!system_source_valid(src,stride,*format))return 0;
    if(sequence==last_snapshot_sequence&&now-last_snapshot_at<100000)return 0;
    /* GE activity may target a different backbuffer. It is diagnostic, not
     * proof that our displayed source is being written. */
    if(sceGeDrawSync(1)!=PSP_GE_LIST_DONE)ge_busy++;
    unsigned long long begin=sceKernelGetSystemTimeWide();request_at=begin;
    copy_source(src,stride,*format);
    *format=fs_output_format(*format);
    unsigned elapsed=(unsigned)(sceKernelGetSystemTimeWide()-begin);
    copy_total_us+=elapsed;if(elapsed>copy_max_us)copy_max_us=elapsed;
    intr=sceKernelCpuSuspendIntr();
    int valid=fs_snapshot_layout_valid(layout,source_layout)&&sequence==frames&&!game_writers&&!saved_overlay&&!saved_aux&&!cancelled&&!suspended&&running;
    sceKernelCpuResumeIntr(intr);
    if(!valid){copy_rejected++;return 0;}
    if(sequence==last_snapshot_sequence)timeout_snapshots++;
    last_snapshot_sequence=sequence;last_snapshot_at=now;
    copied_frames++;return 1;
}
static void record(const char *event,int result)
{
    unsigned long long now=sceKernelGetSystemTimeWide();
    char text[256];int n=snprintf(text,sizeof(text),"%u.%06u %s rc=%08X active=%d frames=%u src=%08X stride=%u fmt=%u\n",
        (unsigned)(now/1000000),(unsigned)(now%1000000),event,result,active,frames,frame_addr,frame_stride,frame_format);
    if(n<0)return;
    if(n>=(int)sizeof(text))n=sizeof(text)-1;
    int fd=sceIoOpen("ms0:/SEPLUGINS/FuSaFullscreen/fullscreen.log",PSP_O_WRONLY|PSP_O_CREAT|PSP_O_APPEND,0666);
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
        if(bytes==0x200000)for(unsigned index=0;index<3;index++) {
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
static int power_event(int unknown,int flags,void *arg)
{
    (void)unknown;(void)arg;
    if(flags&(PSP_POWER_CB_SUSPENDING|PSP_POWER_CB_STANDBY)){suspended=1;request_stop(STOP_SUSPEND,flags);}
    if(flags&PSP_POWER_CB_RESUME_COMPLETE){suspended=0;request_stop(STOP_RESUME,flags);}
    return 0;
}
static int restore(void)
{
    if(!expanded&&!internal_hooked&&!mode_hooked)return 0;
    if(suspended)return -1; /* No hardware changes until resume. */
    if(retiring)return -1; /* Sony owns the display already; never replay it. */
    /* Stop virtualizing primary/mode writes before handoff. Layer 2 is already
     * registered with Sony and must not be replayed from an older snapshot. */
    restoring=1;
    remove_wait_hooks();
    int external=stop_reason==STOP_MODE;
    int rc=external?0:dve_mode(0,old_mode,old_w,old_h,1,15,0);
    int overlay_rc=0,frame_rc=0;
    /* On an external mode change preserve Sony's new layer state, except
     * layers still pointing at our buffers. Do not replay stale TV geometry. */
    {
        int layer=0;
        void *addr=NULL;int stride=0,format=0,sync=0;
        int query=get_internal(layer,&addr,&stride,&format,&sync);
        unsigned p=(unsigned)addr&0x1fffffffU;
        if(!external || (query>=0&&p>=FS_OUTPUT_BASE&&p<FS_OUTPUT_BASE+2*FS_OUTPUT_BYTES)) {
            overlay_rc=set_internal(0,saved_overlay,saved_stride,saved_format,1);
        } else if(query<0)frame_rc=query;
    }
    sceKernelDelayThread(50000);
    /* Do not shrink VRAM if a failed restoration might still scan its upper half. */
    if(rc<0||overlay_rc<0||frame_rc<0){request_stop(STOP_RESTORE,rc<0?rc:overlay_rc<0?overlay_rc:frame_rc);record("restore failed; retain expanded VRAM",stop_detail);return -1;}
    /* Release capture first, but leave physical VRAM mapped if either Sony
     * layer still references its upper half during a display transition. */
    if(expanded){rc=change_edram(0x200000);if(rc>=0)expanded=0;else retiring=1;}
    int intr=sceKernelCpuSuspendIntr();active=0;restoring=0;
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
    record("mode calls remapped before transition",mode_remaps);
    record("timeout-driven source snapshots",timeout_snapshots);
    record("snapshot GE busy polls",ge_busy);
    unsigned copies=copied_frames+copy_rejected;
    record("snapshot copy average us",copies?(int)(copy_total_us/copies):0);
    record("snapshot copy maximum us",copy_max_us);
    record("coordinated game VBlank calls",wait_calls);
    record("coordinated game VBlank timeouts",wait_timeouts);
    record("coordinated game wait average us",wait_calls?(int)(coordinated_us/wait_calls):0);
    return rc;
}
static int start_scale(void)
{
    void *src=NULL;int stride=0,fmt=-1;
    if(suspended||expanded||internal_users||internal_hooked||mode_hooked||mode_users||mode_query_users||wait_users)return -1;
    if(cable_type()!=2){record("component cable required",-1);return -1;}
    if(sceDisplayGetMode(&old_mode,&old_w,&old_h)<0||!fs_game_tv_layout(old_mode,old_w,old_h)){
        record("start in Sony game 480p TV mode",-1);record("start mode",old_mode);
        record("start width",old_w);record("start height",old_h);return -1;}
    if(sceGeEdramGetSize()!=0x200000){record("VRAM already expanded; refuse ownership",-1);return -1;}
    logical_mode=old_mode;logical_width=old_w;logical_height=old_h;
    memset(osd_updated,0,sizeof(osd_updated));
    mode_queries=mode_events=0;
    if(sceDisplayGetFrameBuf(&src,&stride,&fmt,1)<0||!system_source_valid((uintptr_t)src,stride,fmt)){
        record("requires nonoverlapping 16/32-bit source frame",-1);record("start source address",(int)src);
        record("start source stride",stride);record("start source format",fmt);return -1;}
    int saved_sync=0;
    if(get_internal(0,&saved_overlay,&saved_stride,&saved_format,&saved_sync)<0){record("cannot save primary layer",-1);return -1;}
    record("saved primary layer address",(int)saved_overlay);
    record("saved primary layer stride",saved_stride);
    record("saved primary layer format",saved_format);
    if(saved_overlay&&!system_source_valid((uintptr_t)saved_overlay,saved_stride,saved_format))return -1;
    if(get_internal(1,&saved_aux,&aux_stride,&aux_format,&saved_sync)<0){record("cannot save auxiliary layer",-1);return -1;}
    if(saved_aux&&!system_source_valid((uintptr_t)saved_aux,aux_stride,aux_format)){record("unsupported auxiliary source",(int)saved_aux);return -1;}
    overlay_copies=overlay_rejected=0;overlay_sequence++;
    mode_remaps=0;timeout_snapshots=0;last_snapshot_at=0;last_snapshot_sequence=~0U;
    frame_addr=(unsigned)src;frame_stride=stride;frame_format=fmt;frames=0;cancelled=0;
    wait_calls=wait_timeouts=0;coordinated_us=0;
    restoring=0;
    stop_reason=STOP_NONE;stop_detail=0;stop_addr=stop_stride=stop_format=stop_sync=0;
    scaled_frames=changed_during_scale=scale_max_us=0;scale_total_us=0;
    copied_frames=copy_rejected=ge_busy=copy_max_us=0;copy_total_us=0;
    request_at=0;sceKernelClearEventFlag(offered_event,0);
    displayed_base=FS_OUTPUT_BASE;displayed_format=fs_output_format(fmt);
    int rc=install_internal_hook();record("system display hook",rc);
    if(rc<0){record("system entry instruction 0",((unsigned *)internal_entry)[0]);
        record("system entry instruction 1",((unsigned *)internal_entry)[1]);
        record("mode entry instruction 0",((unsigned *)mode_entry)[0]);record("mode entry instruction 1",((unsigned *)mode_entry)[1]);
        remove_internal_hook();return rc;}
    rc=change_edram(0x400000);record("expand VRAM",rc);if(rc<0){remove_internal_hook();return rc;}expanded=1;
    int intr=sceKernelCpuSuspendIntr();active=1;
    sctrlHENPatchSyscall((void *)get_edram_size,(void *)game_edram_size);
    sctrlHENPatchSyscall((void *)get_mode_original,(void *)game_get_mode);query_hooked=1;
    sceKernelCpuResumeIntr(intr);
    memset((void *)(FS_OUTPUT_BASE|0x40000000U),0,2*FS_OUTPUT_BYTES);
    /* Never scale live game VRAM, including the first frame. The cleared
     * output stays black until the worker accepts a RAM snapshot. */
    rc=dve_mode(0,0x1d2,720,480,1,15,0);record("720x480 output",rc);
    if(rc>=0)rc=set_internal(0,(void *)FS_OUTPUT_BASE,768,fs_output_format(fmt),1);
    displayed_base=FS_OUTPUT_BASE;displayed_format=fs_output_format(fmt);
    record("first fullscreen frame",rc);
    if(rc>=0){rc=install_wait_hooks();record("experimental coordinated VBlank enabled",rc<0?rc:wait_hooks);}
    if(rc<0){restore();return rc;}
    return 0;
}
static int setup(void)
{
    /* Resolve capabilities, not a plugin/model/firmware allowlist. Overlays
     * and Consolizer may coexist; source and VRAM ownership are checked live. */
    mode_entry=(void *)sctrlHENFindFunction("sceDisplay_Service","sceDisplay",0x0E20F177);
    get_mode_original=(void *)sctrlHENFindFunction("sceDisplay_Service","sceDisplay",0xDEA197D4);
    set_mode_original=mode_entry;
    set_internal=(void *)sctrlHENFindFunction("sceDisplay_Service","sceDisplay_driver",0x63E22A26);
    internal_entry=set_internal;
    get_internal=(void *)sctrlHENFindFunction("sceDisplay_Service","sceDisplay_driver",0x5B5AEFAD);
    edram_size=(void *)sctrlHENFindFunction("sceGE_Manager","sceGe_driver",0x5BAA5439);
    get_edram_size=(void *)sctrlHENFindFunction("sceGE_Manager","sceGe_driver",0x1F6752AD);
    if(!sceKernelFindModuleByName("pspDveManager_Module")) {
        int id=sceKernelLoadModule("ms0:/SEPLUGINS/FuSaFullscreen/dvemgr.prx",0,NULL),status;
        if(id<0)return id;
        int rc=sceKernelStartModule(id,0,NULL,&status,NULL);if(rc<0)return rc;
    }
    dve_mode=(void *)sctrlHENFindFunction("pspDveManager_Module","pspDveManager_driver",0xF9C86C73);
    cable_type=(void *)sctrlHENFindFunction("pspDveManager_Module","pspDveManager_driver",0x2ACFCB6D);
    return mode_entry&&get_mode_original&&set_internal&&get_internal&&edram_size&&get_edram_size&&dve_mode&&cable_type?0:-3;
}
static int work(SceSize size,void *args)
{
    (void)size;(void)args;
    record("FuSaFullscreen 0.26 standalone release",sceKernelDevkitVersion());
    int osd_rc=sceIoAddDrv(&osd_driver);osd_registered=osd_rc>=0;record("fullscreen OSD mailbox",osd_rc);
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
    /* Resolve real partitions once, never assume that every model has 64 MiB.
     * Only the system layer gains RAM access; game source rules are unchanged. */
    /* Slim impose's ABBBC000 is in extended system RAM (partition 8),
     * distinct from partition 11 used to allocate our snapshot. Read only. */
    const int partitions[4]={2,5,8,11};
    for(unsigned i=0;i<4;i++) {
        PspSysmemPartitionInfo info={.size=sizeof(info)};
        if(sceKernelQueryMemoryPartitionInfo(partitions[i],&info)>=0) {
            unsigned start=info.startaddr&0x1fffffffU;
            if(start>=0x08000000U&&start<0x0c000000U&&info.memsize<=0x0c000000U-start) {
                system_ram_start[i]=start;system_ram_end[i]=start+info.memsize;
                record("system RAM partition",partitions[i]);record("system RAM start",start);record("system RAM end",start+info.memsize);
            }
        }
    }
    callback=sceKernelCreateCallback("fullscreen power",power_event,NULL);
    int automatic,last;
    if(callback>=0)power_slot=oc_register_power_callback(callback,&automatic,&last);
    if(power_slot<0){record("no power callback; activation unavailable",power_slot);
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
    if(offered_event<0){record("capture event allocation failed",offered_event);running=0;}
    unsigned long long started_at=0,retry_at=0,diagnostic_next=0;unsigned previous=0,last_output_vblank=0,logged_mode_events=0;int output_clock_valid=0,back=1,logged_system=-1;
    FsAutoZoom automatic_zoom={0};unsigned long long auto_poll=0;int zoom_armed=0;
    while(running) {
        SceCtrlData pad={0};sceCtrlPeekBufferPositive(&pad,1);
        unsigned chord=PSP_CTRL_NOTE|PSP_CTRL_RTRIGGER;
        unsigned long long now=sceKernelGetSystemTimeWide();
        if(active&&mode_events!=logged_mode_events) {
            int intr=sceKernelCpuSuspendIntr();
            int m=requested_mode,w=requested_width,h=requested_height,result=requested_result,route=requested_route;
            logged_mode_events=mode_events;
            sceKernelCpuResumeIntr(intr);
            char message[112];
            snprintf(message,sizeof(message),"game mode request mode=%X size=%dx%d events=%u queries=%u route=%d",m,w,h,logged_mode_events,mode_queries,route);
            record(message,result);
        }
        if(pad.Buttons&PSP_CTRL_SCREEN){int intr=sceKernelCpuSuspendIntr();screen_until=now+6000000ULL;sceKernelCpuResumeIntr(intr);}
        if(active&&!suspended&&now>=diagnostic_next) {
            diagnostic_next=now+10000000ULL;
            void *actual=NULL;int stride=0,format=0,sync=0;
            int query=get_internal(2,&actual,&stride,&format,&sync);
            char message[144];
            snprintf(message,sizeof(message),"state outputs=%u rejected=%u modes=%u queries=%u actual_game=%08X",
                scaled_frames,copy_rejected,mode_remaps,mode_queries,(unsigned)actual);
            record(message,query);
        }
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
                if(start_scale()==0){zoom_armed=1;started_at=sceKernelGetSystemTimeWide();output_clock_valid=0;retry_at=0;back=1;}
            }
        }
        previous=pad.Buttons;
        if(fs_zoom_wanted(auto_zoom,keep_fullscreen,zoom_armed)&&!active&&!suspended&&!expanded&&!internal_hooked&&!mode_hooked&&now>=auto_poll) {
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
                if(start_rc==0){zoom_armed=1;started_at=sceKernelGetSystemTimeWide();output_clock_valid=0;retry_at=0;back=1;}
                else if(keep_fullscreen&&zoom_armed)automatic_zoom=(FsAutoZoom){0};
            }
        }
        if(active&&!cancelled&&!suspended) {
            unsigned format;
            int mode=0,w=0,h=0;
            int mode_rc=sceDisplayGetMode(&mode,&w,&h);
            unsigned vram=sceGeEdramGetSize();
            if(mode_rc<0||mode!=0x1d2||w!=720||h!=480||vram!=0x400000){
                observed_mode=mode;observed_w=w;observed_h=h;observed_vram=vram;
                request_stop(STOP_MODE,mode_rc);continue;}
            unsigned dest=FS_OUTPUT_BASE+back*FS_OUTPUT_BYTES;
            sceKernelChangeThreadPriority(0,24);
            sceDisplayWaitVblankStart();
            unsigned vblank=(unsigned)sceDisplayGetVcount();
            if(!fs_output_due(output_clock_valid,last_output_vblank,vblank)||cancelled||suspended||!running) {
                sceKernelChangeThreadPriority(0,0x38);continue;
            }
            int captured=take_snapshot(&format);
            sceKernelChangeThreadPriority(0,0x38);
            if(!captured){
                SceUInt timeout=2000;
                int wait_rc=sceKernelWaitEventFlagCB(offered_event,1,PSP_EVENT_WAITOR|PSP_EVENT_WAITCLEAR,NULL,&timeout);
                if(wait_rc<0&&wait_rc!=FS_WAIT_TIMEOUT)request_stop(STOP_WAIT,wait_rc);
                continue;
            }
            last_output_vblank=vblank;output_clock_valid=1;
            unsigned sequence=frames;
            unsigned long long begin=sceKernelGetSystemTimeWide();
            fs_scale16((void *)(dest|0x40000000U),snapshot,480);
            unsigned elapsed=(unsigned)(sceKernelGetSystemTimeWide()-begin);
            scaled_frames++;scale_total_us+=elapsed;if(elapsed>scale_max_us)scale_max_us=elapsed;
            if(frames!=sequence)changed_during_scale++;
            if(cancelled||suspended)continue;
            draw_output_osd(dest,format);
            rc=set_internal(0,(void *)dest,768,format,1);
            if(rc<0){request_stop(STOP_PRESENT,rc);}
            else {
                int intr=sceKernelCpuSuspendIntr();
                displayed_base=dest;displayed_format=format;
                sceKernelCpuResumeIntr(intr);
            }
            back^=1;
            int system=fs_selected_source(saved_overlay!=NULL,saved_aux!=NULL);
            if(system!=logged_system) {
                logged_system=system;record("source layer selected (0 system, 1 auxiliary, 2 game)",system);
                if(system!=2){record("menu source",(int)(system==0?saved_overlay:saved_aux));record("menu stride",system==0?saved_stride:aux_stride);record("menu format",system==0?saved_format:aux_format);}
            }
        }
        /* Active pacing is driven by real vblanks, not a second time delay. */
        unsigned delay=active?2000:20000;
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
    running=1;worker=sceKernelCreateThread("fullscreen scaler",work,0x38,8192,PSP_THREAD_ATTR_VFPU,NULL);
    if(worker<0)return worker;
    int rc=sceKernelStartThread(worker,0,NULL);
    if(rc<0){running=0;sceKernelDeleteThread(worker);worker=-1;}
    return rc;
}
int module_stop(SceSize size,void *args)
{
    (void)size;(void)args;running=0;
    if(offered_event>=0)sceKernelSetEventFlag(offered_event,1);
    if(worker>=0){SceUInt wait=2000000;if(sceKernelWaitThreadEnd(worker,&wait)<0)return -1;}
    remove_wait_hooks();
    if(active||expanded||internal_hooked||internal_users||mode_hooked||mode_users||query_hooked||mode_query_users||wait_users)return -1; /* Never unload live hooks. */
    if(osd_registered){int rc=sceIoDelDrv("fusafullscreen");if(rc<0)return rc;osd_registered=0;}
    if(offered_event>=0){sceKernelDeleteEventFlag(offered_event);offered_event=-1;}
    if(snapshot_block>=0){sceKernelFreePartitionMemory(snapshot_block);snapshot_block=-1;snapshot=NULL;}
    if(worker>=0){sceKernelDeleteThread(worker);worker=-1;}return 0;
}
/* Exit-to-VSH is a reboot, not necessarily an ordinary module unload.
 * Restore while worker/driver services are still available, before the late
 * reboot phase. Never leave our upper-VRAM primary buffer without a producer. */
int module_reboot_before(SceSize size,void *args)
{
    record("reboot before: release fullscreen output",0);
    return module_stop(size,args);
}
