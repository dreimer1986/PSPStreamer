/* SPDX-License-Identifier: MIT */
#ifndef STREAMER_OC_OVERLAY_VBLANK_H
#define STREAMER_OC_OVERLAY_VBLANK_H
static int oc_overlay_vblank(void) {
    unsigned long long deadline=sceKernelGetSystemTimeWide()+20000ULL;
    while(running && !suspended) {
        int blank=sceDisplayIsVblank();
        if(blank>0)return 1;
        if(blank<0 || (unsigned long long)sceKernelGetSystemTimeWide()>=deadline)return 0;
        sceKernelDelayThreadCB(200);
    }
    return 0;
}
#endif
