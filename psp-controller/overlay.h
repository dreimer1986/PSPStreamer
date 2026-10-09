/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <pspdisplay.h>
#include <pspge.h>
#include "../psp-overclock/control_api.h"
#include "../psp-overclock/fullscreen_osd.h"
static int pad_osd_x;
#define OC_OSD_W 212
#define OC_OSD_X pad_osd_x
#define OC_OSD_POSITION(width) (pad_osd_x=(width)-OC_OSD_W-8)
#include "../psp-overclock/overlay_pixels.h"
#define running controller_running
#define suspended controller_suspended
static int oc_overlay_vblank(void){return sceDisplayIsVblank()>0;}
#define OC_HOOK_DYNAMIC_BUFFERS 1
#include "../psp-overclock/overlay_hook.h"
#undef running
#undef suspended
static int pad_overlay_enabled=1,pad_overlay_always,pad_overlay_shared;
static int pad_overlay_pending;
static SceUID pad_overlay_memory=-1;
static unsigned long long pad_overlay_retry,pad_overlay_deadline;
static unsigned long long pad_overlay_start_after;
static int pad_present_render(const void *base,int stride,int format) {
    if(!controller_running || controller_suspended || !oc_hook_lock())return 0;
    int rc=oc_hook_render(base,stride,format);oc_hook_unlock();return rc;
}
static int pad_overlay_init(void) {
    /* Do not reserve pixel memory for an OSD that is switched off.
     * Polling needs one backup, presentation mode needs up to three. */
    if(pad_overlay_enabled && !oc_hook_buffers){
        int count=pad_overlay_enabled==2?3:1;
        unsigned bytes=count*sizeof(OcOverlay);
        /* Pixel backups belong in user RAM, not the scarce codec/kernel
         * partition. Failure disables only the OSD; no kernel fallback. */
        pad_overlay_memory=sceKernelAllocPartitionMemory(2,"Consolizer OSD",PSP_SMEM_High,bytes,NULL);
        if(pad_overlay_memory<0){
            int rc=pad_overlay_memory;pad_overlay_enabled=0;
            controller_log("OSD allocation failed: input retained",rc);return rc;
        }
        oc_hook_buffers=sceKernelGetBlockHeadAddr(pad_overlay_memory);
        if(!oc_hook_buffers){
            sceKernelFreePartitionMemory(pad_overlay_memory);pad_overlay_memory=-1;
            pad_overlay_enabled=0;controller_log("OSD address unavailable: input retained",-1);return -1;
        }
        memset(oc_hook_buffers,0,bytes);oc_hook_buffer_count=count;
        controller_log("OSD pixel memory partition",2);
    }
    if(pad_overlay_enabled!=2)return 0;
    if(sceKernelFindModuleByName("StreamerOC")){
        int (*cb)(const void *,int,int)=pad_present_render;
        int rc=sceIoDevctl(OC_DEVICE,OC_CMD_OSD_ATTACH,&cb,sizeof(cb),NULL,0);
        if(rc==0){pad_overlay_shared=1;pad_overlay_pending=0;return 0;}
        /* OC deliberately finishes its clock startup before publishing its
         * driver. NODEV is therefore normal early in the same launch. Do not
         * silently fall back to a different renderer in mode 2. */
        pad_overlay_pending=1;
        if(!pad_overlay_deadline)pad_overlay_deadline=sceKernelGetSystemTimeWide()+20000000;
        pad_overlay_retry=sceKernelGetSystemTimeWide()+1000000;
        return rc;
    }
    int rc=oc_hook_install();pad_overlay_pending=0;
    if(rc<0)pad_overlay_enabled=0;
    return rc;
}
static void pad_overlay_start(void) {
    /* The boot animation's present hook must not draw/restore OSD pixels
     * while VSH is producing its startup sound. Defer ALL OSD setup, not
     * the controller/audio service and not just overlay visibility. */
    if(pad_overlay_enabled && sceKernelInitKeyConfig()==PSP_INIT_KEYCONFIG_VSH) {
        pad_overlay_start_after=sceKernelGetSystemTimeWide()+5000000ULL;
        controller_log("VSH overlay initialization deferred ms",5000);
    } else controller_log("overlay initialization",pad_overlay_init());
}
static int pad_overlay_stop(void) {
    pad_overlay_start_after=0;
    oc_hook_publish(NULL,0);
    SceInt64 deadline=sceKernelGetSystemTimeWide()+100000;
    if(pad_overlay_shared){
        int rc;
        do {rc=sceIoDevctl(OC_DEVICE,OC_CMD_OSD_DETACH,NULL,0,NULL,0);if(rc!=1)break;sceKernelDelayThread(1000);}while(sceKernelGetSystemTimeWide()<deadline);
        if(rc==1)return -1;
        pad_overlay_shared=0;
    }
    oc_hook_remove();
    while(oc_hook_users && sceKernelGetSystemTimeWide()<deadline)sceKernelDelayThread(1000);
    if(oc_hook_users)return -1;
    if(pad_overlay_memory>=0){
        sceKernelFreePartitionMemory(pad_overlay_memory);pad_overlay_memory=-1;
        oc_hook_buffers=NULL;oc_hook_buffer_count=0;
    }
    return 0;
}
static void pad_overlay_update(int state,int error,int is_suspended) {
    /* A configured-off OSD must not even query the game's display path. */
    if(!pad_overlay_enabled)return;
    static int previous=-1,previous_error;
    static SmPadMeta previous_meta;
    static unsigned long long until,next;
    unsigned long long now=sceKernelGetSystemTimeWide();
    if(pad_overlay_start_after) {
        if(is_suspended || now<pad_overlay_start_after)return;
        pad_overlay_start_after=0;
        int rc=pad_overlay_init();
        controller_log("VSH deferred overlay initialization",rc);
        if(rc<0 || !pad_overlay_enabled)return;
    }
    if(pad_overlay_pending){
        if(is_suspended || now<pad_overlay_retry)return;
        if(now>=pad_overlay_deadline){
            pad_overlay_enabled=0;pad_overlay_pending=0;
            controller_log("OC overlay unavailable: OSD disabled, input retained",-1);return;
        }
        int rc=pad_overlay_init();
        if(rc){return;}
        controller_log("OC overlay attached after startup",0);
    }
    SmPadMeta meta={.battery=255};int intr=sceKernelCpuSuspendIntr();
    unsigned long long meta_now=sceKernelGetSystemTimeWide();
    if(pad_meta_time && meta_now>=pad_meta_time && meta_now-pad_meta_time<30000000)meta=pad_meta;
    sceKernelCpuResumeIntr(intr);
    if(state!=previous || error!=previous_error || memcmp(&meta,&previous_meta,sizeof(meta))){
        until=now+5000000;previous=state;previous_error=error;previous_meta=meta;
    }
    if(is_suspended){oc_hook_publish(NULL,0);if(oc_output_external)fs_osd_publish(1,NULL,0);return;}
    if(now<next)return;
    next=now+33333;
    oc_hook_set_external_output(fs_osd_active());
    char lines[3][40]={{0}};
    snprintf(lines[0],40,"%.33s",meta.valid && meta.name[0]?meta.name:"PSP CONSOLIZER");
    for(unsigned i=0;i<sizeof(lines[0]);i++){
        unsigned char c=lines[0][i];if(c>='a' && c<='z')lines[0][i]=c-'a'+'A';
        else if(c>=128)lines[0][i]='?';
    }
    const char *status=state==6?"USB RELEASED":state==1?"CONNECTED - GAME":state==2?"CONNECTED - APP":state==3?"SUSPENDED":state==4?"DISABLED":state==5?"USB DISCONNECTED":meta.valid && meta.state==SM_BT_CONNECTING?"BT CONNECTING":"BT DISCONNECTED";
    snprintf(lines[1],40,"%s",status);
    if(error<0)snprintf(lines[2],40,"USB ERROR %08X",(unsigned)error);
    else if(state==6)snprintf(lines[2],40,"NOTE VOLDOWN 2S - RESUME");
    else if(meta.valid && meta.error)snprintf(lines[2],40,"BT ERROR %08X",(unsigned)meta.error);
    else if((state==1 || state==2) && meta.valid && meta.battery<=100)snprintf(lines[2],40,"BATTERY %u PERCENT",meta.battery);
    else snprintf(lines[2],40,"NOTE VOLUP 2S - DISABLE");
    int visible=pad_overlay_enabled && (pad_overlay_always || now<until);
    oc_hook_publish(lines,visible);
    if(oc_output_external){fs_osd_publish(1,lines,visible);return;}
    if(oc_hook_installed){oc_hook_idle_refresh(0);return;}
    if(sceDisplayIsVblank()>0){
        void *base=NULL;int stride,format;
        if(sceDisplayGetFrameBuf(&base,&stride,&format,PSP_DISPLAY_SETBUF_IMMEDIATE)>=0)pad_present_render(base,stride,format);
    }
}
