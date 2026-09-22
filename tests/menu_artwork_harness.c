#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned int u32;
#define ID_SIZE 512
static unsigned long long now;
static unsigned long long sceKernelGetSystemTimeWide(void){return now;}
static int remote_http_get_budget(const char *path,char *out,int cap,volatile int *running,int budget) {
    assert(strstr(path,"/api/psp-artwork?item=plex."));assert(cap>=133140 && budget==4000);
    if(!*running)return -1;
    unsigned char *p=(unsigned char *)out;
    memcpy(p,"PSPA\x40\x01\xb4\x00\x50\x00\x70\x00",12);
    unsigned int sizes[]={320*180*2,80*112*2};
    for(int i=0;i<2;i++)for(int j=0;j<4;j++)p[12+4*i+j]=(unsigned char)(sizes[i]>>(j*8));
    for(int i=0;i<320*180;i++){p[20+2*i]=0;p[21+2*i]=0xf8;}
    for(int i=0;i<80*112;i++){p[20+sizes[0]+2*i]=31;p[21+sizes[0]+2*i]=0;}
    return 20+sizes[0]+sizes[1];
}
#include "menu_artwork.h"
int main(void) {
    volatile int running=1;
    menu_art_select("plex.42.abc");assert(!menu_art_schedule());
    now=600000;assert(menu_art_schedule());menu_art_download(&running);
    assert(menu_art_pending && !menu_art_active);menu_art_complete(1);
    assert(menu_art_has_cover() && menu_art_changed && !menu_art_schedule());
    static u32 pixels[768*480];
    menu_art_draw(pixels,768,32,60,498,244,0);
    assert(pixels[60*768+32]==63 && pixels[59*768+32]==0 && pixels[304*768+32]==0);
    menu_art_draw(pixels,768,562,128,116,113,1);
    assert(pixels[180*768+620]==0xff0000 && pixels[127*768+620]==0 && pixels[241*768+620]==0);
    memset(pixels,0,sizeof(pixels));
    menu_art_draw(pixels,512,36,29,312,123,0);menu_art_draw(pixels,512,376,70,72,65,1);
    assert(pixels[29*512+36]==63 && pixels[28*512+36]==0);
    assert(pixels[151*512+36]==63 && pixels[152*512+36]==0);
    assert(pixels[100*512+410]==0xff0000 && pixels[138*512+410]==0);
    assert(!menu_art_valid(menu_art_active,19));menu_art_active[12]=1;
    assert(!menu_art_valid(menu_art_active,MENU_ART_BYTES));
    menu_art_select("plex.43.abc");now+=600000;assert(menu_art_schedule());menu_art_download(&running);
    menu_art_select("plex.44.abc");menu_art_complete(1);assert(!menu_art_active && !menu_art_pending);
    now+=600000;assert(menu_art_schedule());menu_art_download(&running);menu_art_complete(0);
    assert(!menu_art_active && !menu_art_schedule());now+=16000000;assert(menu_art_schedule());
    running=0;menu_art_download(&running);menu_art_complete(0);now+=16000000;assert(!menu_art_schedule());
    menu_art_select("");assert(!menu_art_active);
    puts("LCD/TV crop, opacity, cover bounds, stale selection and cancellation OK");
}
