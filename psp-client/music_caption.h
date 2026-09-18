/* Metadata redraws touch only the existing title area, never the receiver or
 * visualization. Fullscreen visualizers deliberately remain overlay-free. */
static void music_caption_line(char out[160],const char *text) {
    int n=0;
    const char *begin=text;
    while(*text && n++<39) {
        text++;
        while(((unsigned char)*text&0xc0)==0x80)text++;
    }
    size_t size=(size_t)(text-begin);
    if(size>159)size=159;
    memcpy(out,begin,size);out[size]=0;
}
static void music_caption(const char *heading,const char *song,int fullscreen) {
    static char previous[400];
    static int previous_tv=-1,previous_fullscreen=-1;
    static unsigned int previous_generation;
    char key[400];
    unsigned int generation=tv_ui_active?tv_music.full_frames:lcd_music.full_frames;
    unsigned int incremental=tv_ui_active?tv_music.incremental_frames:lcd_music.incremental_frames;
    if(fullscreen) {previous_tv=-1;return;}
    snprintf(key,sizeof(key),"%s\n%s",heading,song);
    if(previous_tv==tv_ui_active && previous_fullscreen==fullscreen &&
       incremental && previous_generation==generation && !strcmp(previous,key))return;
    snprintf(previous,sizeof(previous),"%s",key);
    previous_tv=tv_ui_active;previous_fullscreen=fullscreen;previous_generation=generation;
    if(tv_ui_active) {
        if(!display_output.tv || tvout_video_active || !tv_canvas.pixels)return;
        tv_restore_rect(34,65,498,40);
        tv_text(34,65,38,1,TV_CYAN,"%s",heading);
        tv_music_title_bottom=tv_text(34,85,38,1,TV_WHITE,"%s",song);
        sceDisplayWaitVblankStart();
        for(int y=65;y<105;y++)memcpy((u32 *)0x44000000+y*TV_GUI_STRIDE+34,
                                     tv_canvas.pixels+y*TV_GUI_STRIDE+34,498*4);
    } else if(!display_output.tv && !tvout_video_active) {
        sceDisplayWaitVblankStart();
        char first[160],second[160];
        music_caption_line(first,heading);music_caption_line(second,song);
        lcd_music_restore(38,40,310,20,0);
        gui_text(38,40,0x0000D8FF,"%s",first);
        gui_text(38,52,0x00FFFFFF,"%s",second);
    }
}
