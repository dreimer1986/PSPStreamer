/* SPDX-License-Identifier: GPL-2.0-or-later
 * Runtime modes from nxdk: never alter region or EEPROM flags.
 * Destroy SDL's surfaces BEFORE replacing the physical framebuffer.
 */
static VIDEO_MODE display_modes[16],display_previous;
static int display_count,display_confirm;static Uint32 display_deadline;
static void display_list(void){
    display_count=0;
    for(int hz=50;hz<=60;hz+=10){void *cursor=NULL;VIDEO_MODE m;
        while(XVideoListModes(&m,32,hz,&cursor)&&display_count<16)display_modes[display_count++]=m;
    }
}
static void display_label(const VIDEO_MODE *m,char *s,size_t n){
    int progressive=m->height==720||(m->height==480&&(XVideoGetEncoderSettings()&VIDEO_MODE_480P)&&
        (XVideoGetEncoderSettings()&VIDEO_ADAPTER_MASK)==AV_PACK_HDTV);
    snprintf(s,n,"%dx%d%s / %d Hz",m->width,m->height,progressive?"p":"i",m->refresh);
}
static int display_switch(VIDEO_MODE m){
    if(playing)return 0;
    char note[160];snprintf(note,sizeof(note),"display: switch %dx%d -> %dx%d @ %d",width,height,m.width,m.height,m.refresh);startup_note(note);
    if(fetch.thread)fetch_stop();
    /* Renderer owns every texture, including spectrum and artwork. */
    spectrum_texture=NULL;
    if(renderer)SDL_DestroyRenderer(renderer);renderer=NULL;skin=cover=backdrop=video_texture=NULL;
    if(window)SDL_DestroyWindow(window);window=NULL;
    startup_note("display: window and renderer released");
    SDL_QuitSubSystem(SDL_INIT_VIDEO);
    VIDEO_MODE old=XVideoGetMode();
    if(!XVideoSetMode(m.width,m.height,32,m.refresh))XVideoSetMode(old.width,old.height,32,old.refresh);
    m=XVideoGetMode();width=m.width;height=m.height;
    startup_note("display: hardware mode set");
    if(SDL_InitSubSystem(SDL_INIT_VIDEO)){startup_note("display: SDL init failed");return 0;}
    window=SDL_CreateWindow("PSPStreamer Xbox",0,0,width,height,SDL_WINDOW_SHOWN);
    if(!window){startup_note("display: window creation failed");return 0;}
    if(window)renderer=SDL_CreateRenderer(window,-1,SDL_RENDERER_SOFTWARE);
    if(!renderer){startup_note("display: renderer creation failed");return 0;}
    SDL_Surface *s=IMG_Load("D:\\theme.png");
    if(s&&renderer)skin=SDL_CreateTextureFromSurface(renderer,s);if(s)SDL_FreeSurface(s);
    startup_note(skin?"display: ready for confirmation":"display: theme reload failed");
    return renderer&&skin;
}
