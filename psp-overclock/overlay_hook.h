/* SPDX-License-Identifier: MIT
 * Optional ARK syscall hook. No GE-list injection or display-mode changes.
 * Formatting/register reads remain in the worker. Presentation never waits
 * for VBlank, takes a semaphore, allocates, logs or changes clocks here.
 */
#ifndef STREAMER_OC_OVERLAY_HOOK_H
#define STREAMER_OC_OVERLAY_HOOK_H
#include <systemctrl.h>
typedef int (*OcPresent)(const void *,int,int,int);
static OcPresent oc_present_original;
static volatile int oc_hook_installed,oc_hook_users,oc_hook_busy;
static volatile unsigned int oc_hook_calls;
static volatile unsigned int oc_hook_draws,oc_hook_fallback_draws,oc_hook_rejected;
static unsigned long long oc_hook_last_draw;
static volatile int oc_hook_visible;
static char oc_hook_lines[3][40];
static OcOverlay oc_hook_buffers[3];
static int oc_hook_width,oc_hook_height;
static int oc_hook_lock(void) {
    int intr=sceKernelCpuSuspendIntr();
    int acquired=!oc_hook_busy;if(acquired)oc_hook_busy=1;
    sceKernelCpuResumeIntr(intr);return acquired;
}
static void oc_hook_unlock(void) {__sync_synchronize();oc_hook_busy=0;}
/* Caller holds the shared raster lock. The worker fallback and syscall path
 * must use the same backups, never independently paint over one another. */
static int oc_hook_render(const void *base,int stride,int format) {
    int mode,width,height;
    if(sceDisplayGetMode(&mode,&width,&height)<0 ||
       !oc_osd_layout((uintptr_t)base,sceGeEdramGetSize(),width,height,stride,format)) {
        for(int i=0;i<3;i++)oc_hook_buffers[i].valid=0;
        return 0;
    }
    if(width!=oc_hook_width || height!=oc_hook_height) {
        for(int i=0;i<3;i++)oc_hook_buffers[i].valid=0;
        oc_hook_width=width;oc_hook_height=height;
    }
    volatile void *uncached=(void *)(((uintptr_t)base&0x1fffffffU)|0x40000000U);
    OcOverlay *slot=NULL;
    for(int i=0;i<3;i++)if(oc_hook_buffers[i].base==uncached){slot=&oc_hook_buffers[i];break;}
    if(!slot)for(int i=0;i<3;i++)if(!oc_hook_buffers[i].valid){slot=&oc_hook_buffers[i];break;}
    if(!slot){slot=&oc_hook_buffers[0];slot->valid=0;}
    if(slot->stride!=stride || slot->format!=format)slot->valid=0;
    oc_osd_restore(slot);
    if(oc_hook_visible)oc_osd_draw(slot,uncached,stride,format,oc_hook_lines);
    return 1;
}
static void oc_hook_idle_refresh(int immediate) {
    if(!running || suspended)return;
    if(!oc_hook_lock())return;
    int needed=immediate || sceKernelGetSystemTimeWide()-oc_hook_last_draw>=100000;
    oc_hook_unlock();
    if(!needed || !oc_overlay_vblank() || !running || suspended || !oc_hook_lock())return;
    /* A presentation may have happened during the bounded VBlank wait. */
    if(immediate || sceKernelGetSystemTimeWide()-oc_hook_last_draw>=100000) {
        void *base=NULL;int stride,format;
        if(sceDisplayGetFrameBuf(&base,&stride,&format,PSP_DISPLAY_SETBUF_IMMEDIATE)>=0 &&
           oc_hook_render(base,stride,format)) {
            if(oc_hook_visible)oc_hook_fallback_draws++;
        }
    }
    oc_hook_unlock();
}
static void oc_hook_publish(const char lines[3][40],int visible) {
    if(!oc_hook_lock())return;
    if(lines)memcpy(oc_hook_lines,lines,sizeof(oc_hook_lines));
    oc_hook_visible=visible;
    if(!visible) {
        /* Do not write old VRAM after a mode change/suspend/exit. A hidden
         * overlay is otherwise restored when each buffer is presented again. */
        if(suspended || !running)
            for(int i=0;i<3;i++)oc_hook_buffers[i].valid=0;
    }
    oc_hook_unlock();
}
static int oc_present_hook(const void *base,int stride,int format,int sync) {
    int intr=sceKernelCpuSuspendIntr();oc_hook_users++;sceKernelCpuResumeIntr(intr);
    oc_hook_calls++;
    int result=oc_present_original(base,stride,format,sync);
    int k1=pspSdkSetK1(0);
    if(result>=0 && running && !suspended && oc_hook_installed && oc_hook_lock()) {
        if(oc_hook_render(base,stride,format)) {
            if(oc_hook_visible){oc_hook_draws++;oc_hook_last_draw=sceKernelGetSystemTimeWide();}
        } else oc_hook_rejected++;
        oc_hook_unlock();
    }
    intr=sceKernelCpuSuspendIntr();oc_hook_users--;sceKernelCpuResumeIntr(intr);
    pspSdkSetK1(k1);return result;
}
static int oc_hook_install(void) {
    /* User sceDisplaySetFrameBuf export. Kernel/direct presentation paths
     * deliberately remain untouched. ARK resolves the firmware export NID. */
    oc_present_original=(OcPresent)sctrlHENFindFunction("sceDisplay_Service","sceDisplay",0x289D82FE);
    if(!oc_present_original)return -1;
    int intr=sceKernelCpuSuspendIntr();
    sctrlHENPatchSyscall((void *)oc_present_original,(void *)oc_present_hook);
    oc_hook_installed=1;
    sceKernelCpuResumeIntr(intr);
    return 0;
}
static void oc_hook_remove(void) {
    int intr=sceKernelCpuSuspendIntr();
    oc_hook_visible=0;
    if(oc_hook_installed) {
        oc_hook_installed=0;
        sctrlHENPatchSyscall((void *)oc_present_hook,(void *)oc_present_original);
    }
    sceKernelCpuResumeIntr(intr);
}
#endif
