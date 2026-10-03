#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "overlay_pixels.h"
#define FS_OSD_SERVER 1
#include "fullscreen_osd.h"
static OcOverlay overlay;
static int running=1,suspended,blank_result,become_blank,cancel_at;
static unsigned long long tick;
static unsigned long long sceKernelGetSystemTimeWide(void){return tick;}
static int sceDisplayIsVblank(void){return become_blank && tick>=1000?1:blank_result;}
static void sceKernelDelayThreadCB(int us){tick+=us;if(cancel_at && tick>=400)suspended=1;}
#include "overlay_vblank.h"
int main(void) {
    FsOsdText message={.version=1,.slot=0,.visible=1};
    assert(fs_osd_message_valid(&message));
    message.slot=1;assert(fs_osd_message_valid(&message));
    message.slot=2;assert(!fs_osd_message_valid(&message));
    message.slot=0;message.visible=2;assert(!fs_osd_message_valid(&message));
    message.visible=0;message.version=2;assert(!fs_osd_message_valid(&message));
    _Static_assert(sizeof(overlay.painted)==(OC_OSD_W*OC_OSD_H+7)/8,
                   "overlay colors must remain bit packed");
    assert(!oc_overlay_vblank() && tick==20000);
    tick=0;become_blank=1;assert(oc_overlay_vblank() && tick==1000);
    tick=0;become_blank=0;blank_result=-1;assert(!oc_overlay_vblank() && !tick);
    blank_result=0;cancel_at=1;assert(!oc_overlay_vblank() && tick==400);
    suspended=0;running=0;assert(!oc_overlay_vblank());
    char lines[3][40]={{0}};
    strcpy(lines[0],"OC CPU 382.9 BUS 382.9 MHZ EST");
    strcpy(lines[1],"TARGET 383 SONY 333 MHZ");
    strcpy(lines[2],"ACTIVE ENFORCE ON CB OK");
    assert(oc_osd_layout(0x44000000,2*1024*1024,480,272,512,3));
    assert(oc_osd_layout(0x44000000,2*1024*1024,720,480,768,3));
    assert(!oc_osd_layout(0x441f0000,2*1024*1024,720,480,768,3));
    assert(!oc_osd_layout(0x08800000,2*1024*1024,480,272,512,3));
    assert(!oc_osd_layout(0x44000000,2*1024*1024,480,272,256,3));
    assert(!oc_osd_layout(0x44000000,2*1024*1024,480,272,512,4));
    for(int mode=0;mode<4;mode++) {
        int bytes=mode==3?4:2,size=512*272*bytes;
        unsigned char *image=malloc(size);assert(image);memset(image,0x12,size);
        oc_osd_draw(&overlay,image,512,mode,lines);
        assert(image[0]==0x12 && image[size-1]==0x12);
        oc_osd_set(image,8*512+8,mode,0x1234);
        oc_osd_restore(&overlay);
        assert(oc_osd_get(image,8*512+8,mode)==0x1234);
        oc_osd_set(image,8*512+8,mode,mode==3?0x12121212:0x1212);
        for(int i=0;i<size;i++)assert(image[i]==0x12);
        oc_osd_restore(&overlay);
        /* Reuse the same backup after a dense foreground pattern. Clearing
         * text must clear stored bits too, in every PSP pixel format. */
        for(int pass=0;pass<2;pass++) {
            char changed[3][40];memset(changed,pass?' ':'M',sizeof(changed));
            oc_osd_draw(&overlay,image,512,mode,changed);
            for(int y=0;y<OC_OSD_H;y++)for(int x=0;x<OC_OSD_W;x++) {
                int row=(y-3)/8,col=(x-3)/6;
                int bit=y>=3 && y<27 && x>=3 && x<OC_OSD_W-5 && row<3 && col<38 &&
                    oc_osd_bit(changed[row][col],(x-3)%6,(y-3)%8);
                assert(oc_osd_get(image,(y+8)*512+x+OC_OSD_X,mode)==oc_osd_color(mode,bit));
            }
            oc_osd_restore(&overlay);
            for(int i=0;i<size;i++)assert(image[i]==0x12);
        }
        free(image);
        size=768*480*bytes;image=malloc(size);assert(image);memset(image,0x12,size);
        oc_osd_draw_fresh(image,768,mode,8,236,lines);
        oc_osd_draw_fresh(image,768,mode,500,212,lines);
        for(int y=0;y<480;y++)for(int x=0;x<768;x++) {
            int drawn=y>=8&&y<38&&((x>=8&&x<244)||(x>=500&&x<712));
            unsigned value=oc_osd_get(image,y*768+x,mode);
            if(drawn)assert(value==oc_osd_color(mode,0)||value==oc_osd_color(mode,1));
            else assert(value==(mode==3?0x12121212U:0x1212U));
        }
        free(image);
    }
    return 0;
}
