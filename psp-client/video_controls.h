/* Small reversible overlay on the presented frame, never in decoded staging.
 * Saving only this strip lets Select/Circle close it even while paused. */
static struct {
    int visible, selected, saved, tv;
    u32 underlay[660*48];
} video_controls;
static int video_file_direction;
static void video_controls_background(int save) {
    int tv=video_controls.tv,w=tv?660:440,h=tv?48:32,x=tv?30:20,y=tv?24:16;
    int row,stride=tv?TVOUT_STRIDE:VIDEO_STRIDE;
    u32 *vram=(u32 *)0x44000000;
    for(row=0;row<h;row++) {
        u32 *screen=vram+(y+row)*stride+x,*copy=video_controls.underlay+row*w;
        if(save) memcpy(copy,screen,w*4); else memcpy(screen,copy,w*4);
    }
    video_controls.saved=save;
}
static void video_controls_draw(int paused) {
    int tv=video_controls.tv,i,w=tv?660:440,h=tv?48:32,x=tv?30:20,y=tv?24:16;
    int stride=tv?TVOUT_STRIDE:VIDEO_STRIDE;
    u32 *vram=(u32 *)0x44000000;
    TvCanvas canvas={vram};
    const char *labels[]={"|<","-30s","-10s",tr(paused?TXT_TRANSPORT_RESUME:TXT_TRANSPORT_PAUSE),"+10s","+30s",">|"};
    spectrum_fullscreen_rect(vram,stride,x,y,w,h,0x00141B22);
    for(i=0;i<7;i++) {
        const char *p=labels[i]; int offset=0,cell=w/7;
        spectrum_fullscreen_rect(vram,stride,x+i*cell+2,y+2,cell-4,h-4,
            i==video_controls.selected?0x00604A20:0x002C343C);
        while(*p) {
            int glyph=subtitle_utf8_char(&p);
            if(tv) tv_glyph(&canvas,subtitle_font,glyph,x+i*cell+12+offset,y+16,0x00FFFFFF);
            else gui_draw_small_glyph(vram,glyph,x+i*cell+8+offset,y+12,0x00FFFFFF);
            offset+=tv?12:7;
        }
    }
}
static void video_controls_hide(void) {
    if(video_controls.visible && video_controls.saved) video_controls_background(0);
    video_controls.visible=0;
}
static void video_controls_show(int tv,int paused) {
    video_controls.tv=tv; video_controls.visible=1;
    video_controls_background(1); video_controls_draw(paused);
}
static void video_controls_present(int paused) {
    if(video_controls.visible) { video_controls_background(1); video_controls_draw(paused); }
}
