#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "spectrum_paint.h"
int spectrum_style,spectrum_segments,spectrum_peak_hold;
int spectrum_led_count=20;
enum {W=180,H=480};
static uint32_t pixels[W*H],expected[W*H];
static uint32_t background(int x,int y){return 0x00101010+(x+y)%8;}
static void restore(void *ctx,int x,int y,int w,int h) {
    uint32_t *p=ctx;
    assert(x>=0 && y>=0 && x+w<=W && y+h<=H);
    for(int j=y;j<y+h;j++)for(int i=x;i<x+w;i++)p[j*W+i]=background(i,j);
}
static void oracle(SpectrumPaint *s,int base,int max,int count) {
    restore(expected,0,0,W,H);
    int thickness=max>=160?2:1;
    for(int b=0;b<count;b++)for(int y=base-max;y<base;y++) {
        int level=base-1-y,cap=s->marker[b] && y>=base-s->marker[b] && y<base-s->marker[b]+thickness;
        int lit=y>=base-s->height[b],color_level=level;
        if(spectrum_segments) {
            lit=cap=0;
            for(int j=0;j<spectrum_led_count;j++) {
                int start=j*max/spectrum_led_count,end=(j+1)*max/spectrum_led_count,gap=max>=160?2:1;
                if(gap>=end-start)gap=end-start-1;
                if(level>=start+(j?gap:0) && level<end) {
                    lit=end<=s->height[b];cap=end==s->marker[b];color_level=end-1;break;
                }
            }
        }
        if(cap || lit) {
            uint32_t color=cap?0x00F0F0FF:spectrum_style?spectrum_palette_color(spectrum_style,color_level*100/(max-1)):
                b<count/3?0x0000D8FF:b<2*count/3?0x00B070FF:0x00FFB000;
            for(int x=4+b*25;x<24+b*25;x++)expected[y*W+x]=color;
        }
    }
}
int main(int argc,char **argv) {
    const int sizes[]={81,145,173,168,254,448};
    for(int sz=0;sz<6;sz++)for(spectrum_style=0;spectrum_style<5;spectrum_style++)
    for(spectrum_segments=0;spectrum_segments<2;spectrum_segments++)for(spectrum_peak_hold=0;spectrum_peak_hold<2;spectrum_peak_hold++) {
        SpectrumPaint state={0};restore(pixels,0,0,W,H);int max=sizes[sz],base=max+8;
        for(int frame=0;frame<90;frame++) {
            for(int b=0;b<6;b++) {
                int height=frame<60?(frame*31+b*19)%(max+1):0,y;
                spectrum_paint_bar(&state,pixels,W,4+b*25,base,20,max,b,6,height,
                    1000000ULL+frame*50000ULL,frame==0,restore,pixels,&y);
            }
            oracle(&state,base,max,6);
            assert(!memcmp(pixels,expected,sizeof(pixels)));
        }
    }
    /* Every possible fill height and LED count: each LED is one solid unit,
     * including the topmost one and a moving peak. */
    spectrum_style=1;spectrum_segments=1;spectrum_peak_hold=1;
    for(spectrum_led_count=8;spectrum_led_count<=32;spectrum_led_count++)for(int sz=0;sz<6;sz++) {
        SpectrumPaint led={0};int max=sizes[sz],base=max+8;restore(pixels,0,0,W,H);
        for(int frame=0;frame<=max*2;frame++) {
            int height=frame<=max?frame:2*max-frame,y;
            spectrum_paint_bar(&led,pixels,W,4,base,20,max,0,6,height,1000000ULL+frame*50000ULL,frame==0,restore,pixels,&y);
            oracle(&led,base,max,1);assert(!memcmp(pixels,expected,sizeof(pixels)));
        }
    }
    spectrum_led_count=20;
    SpectrumPaint state={0};int y;
    spectrum_peak_hold=1;spectrum_style=1;spectrum_segments=0;
    spectrum_paint_bar(&state,pixels,W,4,108,20,100,0,6,80,1000000,1,restore,pixels,&y);
    spectrum_paint_bar(&state,pixels,W,4,108,20,100,0,6,20,1400000,0,restore,pixels,&y);
    assert(state.marker[0]==80);
    spectrum_paint_bar(&state,pixels,W,4,108,20,100,0,6,20,1650000,0,restore,pixels,&y);
    assert(state.marker[0]==70);
    spectrum_paint_bar(&state,pixels,W,4,108,20,100,0,6,95,1700000,0,restore,pixels,&y);
    assert(state.marker[0]==95);
    spectrum_paint_bar(&state,pixels,W,4,108,20,100,0,6,0,5000000,0,restore,pixels,&y);
    assert(state.marker[0]==0);
    assert(spectrum_palette_color(1,0)==0x00FF6020 && spectrum_palette_color(1,100)==0x00ED30FF);
    /* Optional real-painter preview: five palettes, with/without segments. */
    if(argc==2) {
        FILE *file=fopen(argv[1],"wb");assert(file);
        for(spectrum_segments=0;spectrum_segments<2;spectrum_segments++)for(spectrum_style=0;spectrum_style<5;spectrum_style++) {
            memset(&state,0,sizeof(state));restore(pixels,0,0,W,H);spectrum_peak_hold=0;
            for(int b=0;b<6;b++)spectrum_paint_bar(&state,pixels,W,4+b*25,248,20,240,b,6,240-b*28,
                1000000,1,restore,pixels,&y);
            assert(fwrite(pixels,4,W*260,file)==W*260);
        }
        fclose(file);
    }
    puts("Spectrum painter: all palettes/segments/peaks match full oracle; peak timing and bounds pass");
}
