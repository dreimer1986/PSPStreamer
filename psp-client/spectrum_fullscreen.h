/* Native LCD/TV spectrum. One clear on entry; thereafter only changed bar
 * strips are touched. No allocation, GU work or display-mode changes. */
static struct {
    int valid, width, height, bars[SPECTRUM_BANDS];
    unsigned long long next_tick;
} spectrum_fullscreen;

static void spectrum_fullscreen_reset(void) {
    memset(&spectrum_fullscreen,0,sizeof(spectrum_fullscreen));
}
static void spectrum_fullscreen_rect(u32 *pixels,int stride,int x,int y,int w,int h,u32 color) {
    int row,col;
    for(row=y; row<y+h; row++)
        for(col=x; col<x+w; col++) pixels[row*stride+col]=color;
}
static void spectrum_fullscreen_render(u32 *pixels,int width,int height,int stride,
                                       const unsigned char *levels,int playing) {
    int band,margin=width/30,base=height-height/30,top=height/30;
    int step=(width-2*margin)/SPECTRUM_BANDS,bar_width=step*3/4;
    if(!spectrum_fullscreen.valid || spectrum_fullscreen.width!=width || spectrum_fullscreen.height!=height) {
        spectrum_fullscreen_rect(pixels,stride,0,0,width,height,0x00080E14);
        memset(spectrum_fullscreen.bars,0,sizeof(spectrum_fullscreen.bars));
        spectrum_fullscreen.valid=1;
        spectrum_fullscreen.width=width; spectrum_fullscreen.height=height;
    }
    for(band=0;band<SPECTRUM_BANDS;band++) {
        int target=playing?levels[band]:0;
        int previous=spectrum_fullscreen.bars[band],h,x=margin+band*step;
        u32 color=band<4?0x0000D8FF:band<8?0x00B070FF:0x00FFB000;
        if(target>100) target=100;
        spectrum_display[band]=music_ui_envelope(spectrum_display[band],target);
        h=spectrum_display[band]*(base-top)/100;
        if(h>previous) spectrum_fullscreen_rect(pixels,stride,x,base-h,bar_width,h-previous,color);
        else if(h<previous) spectrum_fullscreen_rect(pixels,stride,x,base-previous,bar_width,previous-h,0x00080E14);
        spectrum_fullscreen.bars[band]=h;
    }
}
