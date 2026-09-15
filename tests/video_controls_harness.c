#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <sys/mman.h>
#include "tv_canvas.h"
typedef uint32_t u32;
#define TVOUT_STRIDE 768
#define VIDEO_STRIDE 512
#define TXT_TRANSPORT_RESUME 1
#define TXT_TRANSPORT_PAUSE 2
static unsigned char font[81920],*subtitle_font=font;
static const char *tr(int id) { return id==1?"Play":"Pause"; }
static int subtitle_utf8_char(const char **p) { return (unsigned char)*(*p)++; }
static void gui_draw_small_glyph(u32 *p,int glyph,int x,int y,u32 color) { (void)glyph;p[y*512+x]=color; }
static void spectrum_fullscreen_rect(u32 *p,int stride,int x,int y,int w,int h,u32 c) {
    for(int row=y;row<y+h;row++)for(int col=x;col<x+w;col++)p[row*stride+col]=c;
}
#include "video_controls.h"
int main(void) {
    u32 *p=mmap((void *)0x44000000,768*480*4,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    assert(p!=(void *)-1);memset(font,255,sizeof(font));
    video_file_direction=0;assert(!video_file_direction);
    /* Exercise other canvas primitives to keep strict unused checks useful. */
    TvCanvas canvas={p};tv_line(&canvas,0,0,1,1,0);tv_rect(&canvas,0,0,1,1,0);
    for(int tv=0;tv<2;tv++) {
        for(int i=0;i<768*480;i++)p[i]=0x12345678;
        video_controls_show(tv,0);
        for(int button=0;button<7;button++) {video_controls.selected=button;video_controls_draw(button&1);}
        video_controls_hide();
        for(int i=0;i<768*480;i++)assert(p[i]==0x12345678);
        video_controls_show(tv,1);
        for(int i=0;i<768*480;i++)p[i]=0x87654321;
        video_controls_present(1);video_controls_hide();
        for(int i=0;i<768*480;i++)assert(p[i]==0x87654321);
    }
    return 0;
}
