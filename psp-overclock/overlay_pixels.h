/* SPDX-License-Identifier: MIT
 * Small diagnostic rasterizer, independent of application GU/debug state. */
#ifndef STREAMER_OC_OVERLAY_PIXELS_H
#define STREAMER_OC_OVERLAY_PIXELS_H
#include <stdint.h>
#define OC_OSD_W 236
#define OC_OSD_H 30
typedef struct {
    volatile void *base;
    int stride, format, valid;
    uint32_t before[OC_OSD_W*OC_OSD_H], painted[OC_OSD_W*OC_OSD_H];
} OcOverlay;
/* Columns, top bit is the top pixel. Own minimal 5x7 diagnostic alphabet. */
static const unsigned char oc_glyphs[][5]={
    {62,81,73,69,62},{0,66,127,64,0},{66,97,81,73,70},{33,65,69,75,49},
    {24,20,18,127,16},{39,69,69,69,57},{60,74,73,73,48},{1,113,9,5,3},
    {54,73,73,73,54},{6,73,73,41,30},
    {126,17,17,17,126},{127,73,73,73,54},{62,65,65,65,34},{127,65,65,34,28},
    {127,73,73,73,65},{127,9,9,9,1},{62,65,73,73,122},{127,8,8,8,127},
    {0,65,127,65,0},{32,64,65,63,1},{127,8,20,34,65},{127,64,64,64,64},
    {127,2,12,2,127},{127,4,8,16,127},{62,65,65,65,62},{127,9,9,9,6},
    {62,65,81,33,94},{127,9,25,41,70},{70,73,73,73,49},{1,1,127,1,1},
    {63,64,64,64,63},{31,32,64,32,31},{63,64,56,64,63},{99,20,8,20,99},
    {7,8,112,8,7},{97,81,73,69,67}
};
static int oc_osd_bit(char c,int x,int y) {
    if(x>=5 || y>=7)return 0;
    if(c>='0'&&c<='9')return (oc_glyphs[c-'0'][x]>>y)&1;
    if(c>='A'&&c<='Z')return (oc_glyphs[c-'A'+10][x]>>y)&1;
    if(c=='.')return x==2 && y==6;
    if(c=='-')return y==3;
    if(c==':')return x==2 && (y==2||y==5);
    return 0;
}
static uint32_t oc_osd_color(int format,int foreground) {
    /* Opaque white on dark blue, converted to the framebuffer's bit layout. */
    if(foreground)return format==3?0xffffffffU:0xffffU;
    if(format==0)return 0x2800;
    if(format==1)return 0x9400;
    if(format==2)return 0xf200;
    return 0xff280000U;
}
static uint32_t oc_osd_get(volatile void *base,int index,int format) {
    return format==3?((volatile uint32_t *)base)[index]:((volatile uint16_t *)base)[index];
}
static void oc_osd_set(volatile void *base,int index,int format,uint32_t value) {
    if(format==3)((volatile uint32_t *)base)[index]=value;
    else ((volatile uint16_t *)base)[index]=(uint16_t)value;
}
static void oc_osd_restore(OcOverlay *o) {
    if(!o->valid)return;
    for(int y=0;y<OC_OSD_H;y++)for(int x=0;x<OC_OSD_W;x++) {
        int i=y*OC_OSD_W+x,offset=(y+8)*o->stride+x+8;
        /* Do not overwrite pixels the application has since redrawn. */
        if(oc_osd_get(o->base,offset,o->format)==o->painted[i])
            oc_osd_set(o->base,offset,o->format,o->before[i]);
    }
    o->valid=0;
}
static void oc_osd_draw(OcOverlay *o,volatile void *base,int stride,int format,const char lines[3][40]) {
    o->base=base;o->stride=stride;o->format=format;
    for(int y=0;y<OC_OSD_H;y++)for(int x=0;x<OC_OSD_W;x++) {
        int row=(y-3)/8,col=(x-3)/6;
        int bit=y>=3 && y<27 && x>=3 && x<231 && row<3 && col<38 &&
            oc_osd_bit(lines[row][col],(x-3)%6,(y-3)%8);
        int i=y*OC_OSD_W+x,offset=(y+8)*stride+x+8;
        o->before[i]=oc_osd_get(base,offset,format);
        o->painted[i]=oc_osd_color(format,bit);
        oc_osd_set(base,offset,format,o->painted[i]);
    }
    o->valid=1;
}
static int oc_osd_layout(uintptr_t address,unsigned int vram,int width,int height,int stride,int format) {
    if(format<0||format>3||width<OC_OSD_W+8||height<OC_OSD_H+8||
       width>720||height>480||stride<width||stride>1024||stride%16)return 0;
    if(address&15)return 0;
    uintptr_t physical=address&0x1fffffffU;
    if(physical<0x04000000U||physical>=0x04000000U+vram)return 0;
    unsigned int bytes=(format==3?4:2)*stride*height;
    return bytes<=vram-(physical-0x04000000U);
}
#endif
