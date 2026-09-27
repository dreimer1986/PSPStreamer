/* Native LCD/TV spectrum. One clear on entry; thereafter only changed bar
 * strips are touched. No allocation, GU work or display-mode changes. */
#include "spectrum_paint.h"
static SpectrumPaint full_spectrum;
static struct {
    int valid, width, height, count, style, bars[SPECTRUM_MAX_BANDS];
    unsigned long long next_tick;
} spectrum_fullscreen;

static void spectrum_fullscreen_reset(void) {
    memset(&spectrum_fullscreen,0,sizeof(spectrum_fullscreen));
    memset(&full_spectrum,0,sizeof(full_spectrum));
}
static void spectrum_fullscreen_rect(u32 *pixels,int stride,int x,int y,int w,int h,u32 color) {
    int row,col;
    for(row=y; row<y+h; row++)
        for(col=x; col<x+w; col++) pixels[row*stride+col]=color;
}
typedef struct {u32 *pixels;int stride;} SpectrumSurface;
static void full_spectrum_restore(void *ctx,int x,int y,int w,int h) {
    SpectrumSurface *surface=ctx;
    spectrum_fullscreen_rect(surface->pixels,surface->stride,x,y,w,h,0x00080E14);
}
static void spectrum_fullscreen_render(u32 *pixels,int width,int height,int stride,
                                       const unsigned char *levels,int playing) {
    int band,margin=width/30,base=height-height/30,top=height/30;
    int count=spectrum_bar_count(),step=(width-2*margin)/count,bar_width=step*3/4;
    if(spectrum_fullscreen.style!=spectrum_style_key()){spectrum_fullscreen_reset();spectrum_fullscreen.style=spectrum_style_key();}
    if(!spectrum_fullscreen.valid || spectrum_fullscreen.width!=width || spectrum_fullscreen.height!=height || spectrum_fullscreen.count!=count) {
        spectrum_fullscreen_rect(pixels,stride,0,0,width,height,0x00080E14);
        memset(spectrum_fullscreen.bars,0,sizeof(spectrum_fullscreen.bars));
        spectrum_fullscreen.valid=1;
        spectrum_fullscreen.width=width; spectrum_fullscreen.height=height;
        spectrum_fullscreen.count=count;
        memset(&full_spectrum,0,sizeof(full_spectrum));
    }
    SpectrumSurface surface={pixels,stride};
    unsigned long long now=sceKernelGetSystemTimeWide();
    for(band=0;band<count;band++) {
        int target=playing?levels[band]:0;
        int h,x=margin+band*step,dirty_y;
        if(target>100) target=100;
        spectrum_display[band]=music_ui_envelope(spectrum_display[band],target);
        h=spectrum_display[band]*(base-top)/100;
        spectrum_paint_bar(&full_spectrum,pixels,stride,x,base,bar_width,base-top,
            band,count,h,now,0,full_spectrum_restore,&surface,&dirty_y);
        spectrum_fullscreen.bars[band]=h;
    }
}
