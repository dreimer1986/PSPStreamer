/* SPDX-License-Identifier: MIT
 * PSP TV scanout policy, independent of the renderer and firmware imports.
 * PPA videomode.cpp / graphics.c: 0x1d1, 720x503; even picture lines
 * start at row 262, odd lines at row 0. 503 is storage/timing, NOT 503p.
 */
#ifndef PSP_TV_OUTPUT_LAYOUT_H
#define PSP_TV_OUTPUT_LAYOUT_H
#include <stdint.h>
#include <stddef.h>
#define TV_FIELD_ROWS 503
#define TV_FIELD_SECOND 262
enum { TV_SCAN_AUTO, TV_SCAN_INTERLACED, TV_SCAN_PROGRESSIVE };
static inline int tv_model_has_output(int model) {return model>0 && model!=10;}
static inline int tv_cable_supported(int cable) {return cable==1 || cable==2;}
static inline int tv_mode_interlaced(int mode) {return mode==0x1d1 || mode==0x2d1;}
static inline int tv_choose_interlaced(int cable,int preference,int previous_mode) {
    return cable==1 || (cable==2 && (preference==TV_SCAN_INTERLACED ||
        (preference==TV_SCAN_AUTO && tv_mode_interlaced(previous_mode))));
}
static inline unsigned tv_scan_row(unsigned y,int interlaced) {
    return interlaced ? y/2 + ((y&1)?0:TV_FIELD_SECOND) : y;
}
static inline uint16_t tv_pixel_565(uint32_t p) {
    return (uint16_t)(((p>>3)&31)|((p>>5)&0x7e0)|((p>>8)&0xf800));
}
/* Rectangles remain in visible 720x480 coordinates. The caller owns bounds
 * and clears the field gap once when entering the mode. No in-place copy. */
static inline void tv_pack_rect(void *out,const uint32_t *in,int source_stride,
                               int x,int y,int w,int h,int interlaced) {
    for(int row=y;row<y+h;row++) {
        unsigned dest=tv_scan_row((unsigned)row,interlaced)*768U+(unsigned)x;
        if(interlaced) {
            volatile uint16_t *d=(volatile uint16_t *)out+dest;
            for(int col=0;col<w;col++)d[col]=tv_pixel_565(in[row*source_stride+x+col]);
        } else {
            volatile uint32_t *d=(volatile uint32_t *)out+dest;
            for(int col=0;col<w;col++)d[col]=in[row*source_stride+x+col];
        }
    }
}
#endif
