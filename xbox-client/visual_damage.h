/* Retain SDL's existing HD surface; no second HD framebuffer allocation. */
static unsigned music_scene_key(void){
    unsigned hash=2166136261u;
    const char *strings[]={media_id,media_name,media_artist,status};
    for(unsigned i=0;i<sizeof(strings)/sizeof(strings[0]);i++)
        for(const unsigned char *s=(const unsigned char*)strings[i];*s;s++)hash=(hash^*s)*16777619u;
    unsigned values[]={width,height,fullscreen,paused,visual_mode,show_spectrum,volume,
        (unsigned)(uintptr_t)cover,(unsigned)(uintptr_t)backdrop,(unsigned)(uintptr_t)renderer,
        (!fullscreen&&!visual_mode)?(unsigned)player_position():0};
    for(unsigned i=0;i<sizeof(values)/sizeof(values[0]);i++)hash=(hash^values[i])*16777619u;
    return hash;
}
static Uint32 music_damage_draw(void){
    SDL_RenderSetScale(renderer,(float)width/720,(float)height/480);clip(NULL);
    visual_draw(fullscreen);
    SDL_Rect damage[2];int count=0;
    SDL_Rect visual=fullscreen?(visual_mode?(SDL_Rect){0,0,720,480}:(SDL_Rect){30,40,660,385}):
        (visual_mode?(SDL_Rect){27,60,506,232}:(SDL_Rect){37,147,488,135});
    damage[count++]=(SDL_Rect){visual.x*width/720,visual.y*height/480,
        (visual.x+visual.w)*width/720-visual.x*width/720,
        (visual.y+visual.h)*height/480-visual.y*height/480};
    if(!fullscreen){
        SDL_Rect instruments={27,360,666,110},all={0,0,720,480};
        clip(&instruments);SDL_RenderCopy(renderer,skin,NULL,&all);receiver();clip(NULL);
        damage[count++]=(SDL_Rect){27*width/720,360*height/480,
            693*width/720-27*width/720,470*height/480-360*height/480};
    }
    SDL_RenderFlush(renderer);Uint32 ready=SDL_GetTicks();
    SDL_UpdateWindowSurfaceRects(window,damage,count);return ready;
}
