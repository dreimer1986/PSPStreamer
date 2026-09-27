/* SPDX-License-Identifier: GPL-2.0-or-later
 * Display only. Shared LCD/TV painter; no audio work or framebuffer allocation. */
#ifndef PSPSTREAMER_SPECTRUM_PAINT_H
#define PSPSTREAMER_SPECTRUM_PAINT_H
#include <stdint.h>
#include <string.h>
extern int spectrum_style,spectrum_segments,spectrum_peak_hold;
extern int spectrum_led_count;
typedef struct {
    int tv_original;
    int height[64],marker[64];
    float peak[64];
    unsigned long long hold[64],tick[64];
} SpectrumPaint;
typedef void (*SpectrumRestore)(void *,int,int,int,int);
static inline int spectrum_style_key(void){return spectrum_style+8*spectrum_segments+16*spectrum_peak_hold+32*spectrum_led_count;}
static inline int spectrum_leds(int maximum) {
    int n=spectrum_led_count;
    if(n<8)n=8;
    if(n>32)n=32;
    if(n>maximum/2)n=maximum/2;
    return n;
}
static inline int spectrum_snap(int height,int maximum) {
    if(!spectrum_segments)return height;
    int n=spectrum_leds(maximum);
    return (height*n/maximum)*maximum/n;
}
static inline int spectrum_marker_size(int marker,int maximum) {
    if(!spectrum_segments){int size=maximum>=160?2:1;return marker<size?marker:size;}
    if(!marker)return 0;
    int n=spectrum_leds(maximum),k=(marker*n+maximum-1)/maximum;
    int start=(k-1)*maximum/n,cell=marker-start;
    int gap=maximum>=160?2:1;
    if(gap>=cell)gap=cell-1;
    return cell-(k>1?gap:0);
}
static inline uint32_t spectrum_mix_color(uint32_t a,uint32_t b,int t,int range) {
    uint32_t out=0;
    for(int shift=0;shift<24;shift+=8) {
        int x=(a>>shift)&255,y=(b>>shift)&255;
        out|=(uint32_t)(x+(y-x)*t/range)<<shift;
    }
    return out;
}
static inline uint32_t spectrum_palette_color(int style,int p) {
    /* ABGR, low byte = red. Colors are fixed to canvas height, not bar height. */
    static const uint32_t rainbow[]={0x00FF6020,0x00E0EF00,0x0048E020,0x0000EBF0,0x000070FF,0x003020F0,0x00ED30FF};
    static const uint32_t vu[]={0x0030C028,0x0000E0F0,0x001020F0};
    static const uint32_t ice[]={0x00A03010,0x00F0D040,0x00FFFFFF};
    static const uint32_t fire[]={0x00000060,0x000060FF,0x0040E0FF,0x00FFFFFF};
    const uint32_t *colors=rainbow;int n=7;
    if(style==2){colors=vu;n=3;}else if(style==3){colors=ice;n=3;}else if(style==4){colors=fire;n=4;}
    if(p<0)p=0;
    if(p>100)p=100;
    int pos=p*(n-1),i=pos/100;
    return i==n-1?colors[i]:spectrum_mix_color(colors[i],colors[i+1],pos%100,100);
}
static inline const uint32_t *spectrum_palette(int maximum) {
    static uint32_t colors[481];static int last_height,last_style=-1;
    if(last_height!=maximum || last_style!=spectrum_style) {
        for(int i=0;i<=maximum;i++)colors[i]=spectrum_palette_color(spectrum_style,i*100/(maximum-1));
        last_height=maximum;last_style=spectrum_style;
    }
    return colors;
}
static inline void spectrum_paint_region(SpectrumPaint *s,uint32_t *pixels,int stride,
        int x,int base,int width,int maximum,int band,int count,int lo,int hi,
        SpectrumRestore restore,void *ctx) {
    const uint32_t *colors=spectrum_palette(maximum);
    uint32_t original=band<count/3?(s->tv_original?0x003CC9FF:0x0000D8FF):
        band<2*count/3?0x00B070FF:(s->tv_original?0x00EAD080:0x00FFB000);
    int n=spectrum_leds(maximum),thickness=spectrum_marker_size(s->marker[band],maximum);
    for(int y=lo;y<hi;y++) {
        int level=base-1-y;
        int color_level=level,lit=y>=base-s->height[band];
        if(spectrum_segments) {
            int k=((level+1)*n-1)/maximum;
            int start=k*maximum/n,end=(k+1)*maximum/n,gap=maximum>=160?2:1;
            if(gap>=end-start)gap=end-start-1;
            lit=end<=s->height[band] && level>=start+(k?gap:0);
            color_level=end-1; /* A physical LED is one solid color. */
        }
        int cap=s->marker[band] && y>=base-s->marker[band] && y<base-s->marker[band]+thickness;
        if(lit || cap) {
            uint32_t color=cap?0x00F0F0FF:spectrum_style?colors[color_level]:original;
            for(int col=x;col<x+width;col++)pixels[y*stride+col]=color;
        } else restore(ctx,x,y,width,1);
    }
}
/* Return the one dirty vertical span, at most one copy rectangle per band. */
static inline int spectrum_paint_bar(SpectrumPaint *s,uint32_t *pixels,int stride,
        int x,int base,int width,int maximum,int band,int count,int height,
        unsigned long long now,int force,SpectrumRestore restore,void *ctx,int *dirty_y) {
    int old=s->height[band],old_marker=s->marker[band],marker=0;
    if(spectrum_peak_hold) {
        if(height>=s->peak[band]){s->peak[band]=(float)height;s->hold[band]=now+450000;}
        else if(now>s->hold[band]) {
            unsigned long long from=s->tick[band]>s->hold[band]?s->tick[band]:s->hold[band];
            if(now>from)s->peak[band]-=(float)(now-from)*maximum*.0000005f;
            if(s->peak[band]<height)s->peak[band]=(float)height;
        }
        marker=(int)s->peak[band];
    } else s->peak[band]=0;
    height=spectrum_snap(height,maximum);marker=spectrum_snap(marker,maximum);
    int old_size=spectrum_marker_size(old_marker,maximum),size=spectrum_marker_size(marker,maximum);
    s->tick[band]=now;s->height[band]=height;s->marker[band]=marker;
    if(!force && old==height && old_marker==marker)return 0;
    int lo=base,hi=0;
    if(force==1 || old!=height) {
        lo=base-(force==1?height:(old>height?old:height));
        hi=force==1?base:base-(old<height?old:height);
    }
    if(old_marker && (old_marker!=marker || force==1)){int y=base-old_marker;if(y<lo)lo=y;if(y+old_size>hi)hi=y+old_size;}
    if(marker && (old_marker!=marker || force==1)){int y=base-marker;if(y<lo)lo=y;if(y+size>hi)hi=y+size;}
    if(lo<base-maximum)lo=base-maximum;
    if(hi>base)hi=base;
    if(hi<=lo)return 0;
    if(force) spectrum_paint_region(s,pixels,stride,x,base,width,maximum,band,count,lo,hi,restore,ctx);
    else {
        if(old!=height)spectrum_paint_region(s,pixels,stride,x,base,width,maximum,band,count,
            base-(old>height?old:height),base-(old<height?old:height),restore,ctx);
        if(old_marker!=marker) {
            if(old_marker)spectrum_paint_region(s,pixels,stride,x,base,width,maximum,band,count,
                base-old_marker,base-old_marker+old_size,restore,ctx);
            if(marker)spectrum_paint_region(s,pixels,stride,x,base,width,maximum,band,count,
                base-marker,base-marker+size,restore,ctx);
        }
    }
    *dirty_y=lo;return hi-lo;
}
#endif
