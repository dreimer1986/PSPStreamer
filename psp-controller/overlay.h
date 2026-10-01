/* SPDX-License-Identifier: GPL-2.0-or-later
 * Best-effort OSD, no display hook or blocking VBlank wait. */
#include <pspdisplay.h>
#include <pspge.h>
static int pad_osd_x;
#define OC_OSD_W 212
#define OC_OSD_X pad_osd_x
#include "../psp-overclock/overlay_pixels.h"
static OcOverlay pad_osd;
static int pad_overlay_enabled=1,pad_overlay_always;
static void pad_overlay_update(int state,int error,int suspended) {
    static int previous=-1,previous_error;
    static unsigned long long until,next;
    unsigned long long now=sceKernelGetSystemTimeWide();
    if(state!=previous || error!=previous_error){until=now+5000000;previous=state;previous_error=error;}
    if(suspended){pad_osd.valid=0;return;}
    if(now<next || sceDisplayIsVblank()<=0)return;
    next=now+33333;
    if(!pad_osd.valid && (!pad_overlay_enabled || (!pad_overlay_always && now>=until)))return;
    void *base=NULL;int stride,format,mode,width,height;
    if(sceDisplayGetFrameBuf(&base,&stride,&format,PSP_DISPLAY_SETBUF_IMMEDIATE)<0 ||
       sceDisplayGetMode(&mode,&width,&height)<0 ||
       !oc_osd_layout((uintptr_t)base,sceGeEdramGetSize(),width,height,stride,format)){
        pad_osd.valid=0;return;
    }
    void *mapped=(void *)(((uintptr_t)base&0x1fffffffU)|0x40000000U);
    int x=width-OC_OSD_W-8;
    /* Never restore stale data into a different/reused framebuffer. */
    if(pad_osd.base!=mapped || pad_osd.stride!=stride || pad_osd.format!=format || pad_osd_x!=x)pad_osd.valid=0;
    oc_osd_restore(&pad_osd);pad_osd_x=x;
    if(!pad_overlay_enabled || (!pad_overlay_always && now>=until))return;
    char lines[3][40]={{0}};
    snprintf(lines[0],40,"STREAMMASTER BT");
    const char *status=state==1?"CONNECTED":state==2?"CONNECTED - APP":state==3?"SUSPENDED":state==4?"DISABLED":state==5?"USB DISCONNECTED":"BT DISCONNECTED";
    snprintf(lines[1],40,"%s",status);
    if(error<0)snprintf(lines[2],40,"USB ERROR %08X",(unsigned)error);
    else snprintf(lines[2],40,"NOTE VOLUP 2S - DISABLE");
    oc_osd_draw(&pad_osd,mapped,stride,format,lines);
}
