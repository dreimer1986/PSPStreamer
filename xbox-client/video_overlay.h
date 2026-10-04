/* SPDX-License-Identifier: GPL-2.0-or-later
 * NV2A PVIDEO: double-buffered, color-keyed YUY2 scanout. No pbkit/depth/RGB
 * backbuffers. Register semantics checked against NV xf86-video-nv nv_video.c
 * and JayFoxRox/xbox-fps-overlay; see docs/XBOX_GPU_VIDEO.md.
 * Main-thread only. Never wait for a free buffer in the audio service loop.
 */
#include "yuy2_pack.h"
#define XBOX_VIDEO_KEY 0x00010203u
static int video_hardware=1;
static struct {
    uint8_t *memory;
    unsigned width,height,pitch,size,slot;
    int initialized,disabled,active;
    Uint32 busy_since;
    unsigned shown,busy,pack_ms,gui_ms;
} overlay;
#ifndef XBOX_VIDEO_REGISTER
#define XBOX_VIDEO_REGISTER(offset) ((volatile uint32_t *)(uintptr_t)(0xfd008000u+(offset)))
#endif
static volatile uint32_t *overlay_reg(unsigned offset){return XBOX_VIDEO_REGISTER(offset);}
static void overlay_write(unsigned offset,uint32_t value){*overlay_reg(offset)=value;}
static int overlay_hide(void){
    if(!overlay.initialized)return 1;
    overlay_write(0x704,1); /* immediate stop, not a queued frame */
    Uint32 start=SDL_GetTicks();
    while(*overlay_reg(0x700)&0x11){if(SDL_GetTicks()-start>=50)return 0;SDL_Delay(1);}
    overlay.active=0;return 1;
}
static void overlay_close(void){
    /* On a hardware timeout retain the allocation rather than freeing memory
     * which may still be scanned. Software fallback remains available. */
    if(!overlay_hide()){overlay.disabled=1;return;}
    if(overlay.memory)MmFreeContiguousMemory(overlay.memory);
    memset(&overlay,0,sizeof(overlay));
}
static int overlay_prepare(unsigned w,unsigned h){
    if(!video_hardware||overlay.disabled)return 0;
    if(overlay.memory&&overlay.width==w&&overlay.height==h)return 1;
    overlay_close();if(overlay.disabled)return 0;
    if(!w||!h||(w&1)||(h&1)||w>1920||h>1080){overlay.disabled=1;return 0;}
    overlay.pitch=(w*2+63)&~63u;overlay.size=overlay.pitch*h;
    overlay.memory=MmAllocateContiguousMemoryEx(overlay.size*2,0,0x03ffffff,64,PAGE_READWRITE|PAGE_WRITECOMBINE);
    if(!overlay.memory){overlay.disabled=1;return 0;}
    memset(overlay.memory,0,overlay.size*2);__asm__ __volatile__("sfence" : : : "memory");
    overlay.width=w;overlay.height=h;overlay.initialized=1;
    overlay_write(0x704,1);overlay_write(0x140,0);overlay_write(0x100,0x11);
    overlay_write(0xb00,XBOX_VIDEO_KEY);
    for(unsigned i=0;i<2;i++){
        overlay_write(0x900+i*4,0);overlay_write(0x908+i*4,0x03ffffff);
        overlay_write(0x910+i*4,0x1000);overlay_write(0x918+i*4,0x1000);
    }
    return 1;
}
/* Return -1 for persistent hardware trouble, 0 for a busy slot, 1 submitted.
 * Slots still owned by PVIDEO are never overwritten or waited on. */
static int overlay_present(const plm_frame_t *f,SDL_Rect dst){
    unsigned slot=overlay.slot,bit=1u<<(slot*4),reg=slot*4;
    if(*overlay_reg(0x700)&bit){
        overlay.busy++;
        if(!overlay.busy_since)overlay.busy_since=SDL_GetTicks();
        if(SDL_GetTicks()-overlay.busy_since>250){overlay_hide();overlay.disabled=1;return -1;}
        return 0;
    }
    overlay.busy_since=0;
    uint8_t *out=overlay.memory+slot*overlay.size;Uint32 start=SDL_GetTicks();
    xbox_pack_yuy2(out,overlay.pitch,f->y.data,f->y.width,f->cb.data,f->cb.width,f->cr.data,f->cr.width,f->width,f->height);
    overlay.pack_ms+=SDL_GetTicks()-start;
    overlay_write(0x920+reg,(uint32_t)MmGetPhysicalAddress(out));
    overlay_write(0x928+reg,(f->height<<16)|f->width);overlay_write(0x930+reg,0);
    overlay_write(0x938+reg,((f->width-1)<<20)/(dst.w-1));
    overlay_write(0x940+reg,((f->height-1)<<20)/(dst.h-1));
    overlay_write(0x948+reg,(dst.y<<16)|dst.x);overlay_write(0x950+reg,(dst.h<<16)|dst.w);
    /* Match SDL's automatic conversion: BT.601 SD, BT.709 above 576 lines. */
    overlay_write(0x958+reg,overlay.pitch|(1u<<16)|(1u<<20)|(f->height>576?1u<<24:0));
    overlay_write(0x704,0);overlay_write(0x700,bit);
    overlay.slot^=1;overlay.active=1;overlay.shown++;return 1;
}
