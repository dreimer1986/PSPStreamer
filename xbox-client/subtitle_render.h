/* GPL-2.0-or-later. Color-key scanout subtitle layer; no video readback. */
static SDL_Texture *subtitle_textures[3];
static int subtitle_widths[3],subtitle_heights[3],subtitle_lines;
static char subtitle_rendered[160];
static void subtitle_render_clear(void){
    for(int i=0;i<3;i++){if(subtitle_textures[i])SDL_DestroyTexture(subtitle_textures[i]);subtitle_textures[i]=NULL;}
    subtitle_lines=0;subtitle_rendered[0]=0;
}
static int subtitle_render_update(void){
    if(SDL_AtomicGet(&subtitles.failed)){
        SDL_AtomicSet(&subtitles.failed,0);
        snprintf(status,sizeof(status),"%s",visual_text("Subtitle page unavailable; playback continues","Untertitelseite nicht erreichbar; Wiedergabe läuft weiter"));
        startup_note(status);
    }
    char cue[160];subtitle_current((unsigned)((seek_base+(frame_pts<0?0:(double)frame_pts/90000))*1000),cue);
    if(!strcmp(cue,subtitle_rendered))return 0;
    subtitle_render_clear();snprintf(subtitle_rendered,sizeof(subtitle_rendered),"%s",cue);
    char *line=cue;
    for(int i=0;i<3&&*line;i++){
        char *end=strchr(line,'|');if(end)*end=0;
        SDL_Surface *s=TTF_RenderUTF8_Blended(font,line,(SDL_Color){255,255,255,255});
        if(s){subtitle_widths[i]=s->w;subtitle_heights[i]=s->h;
            subtitle_textures[i]=SDL_CreateTextureFromSurface(renderer,s);SDL_FreeSurface(s);}
        subtitle_lines=i+1;if(!end)break;line=end+1;
    }
    return 1;
}
static SDL_Rect subtitle_area(SDL_Rect video){
    int line_height=video.h/18;if(line_height<12)line_height=12;
    int h=3*line_height+video.h/24+2;if(h>video.h)h=video.h;
    return (SDL_Rect){video.x,video.y+video.h-h,video.w,h};
}
static void subtitle_render_draw(SDL_Rect video){
    if(controls||!subtitle_lines)return;
    SDL_RenderSetScale(renderer,1,1);
    int line_height=video.h/18;if(line_height<12)line_height=12;
    int bottom=video.y+video.h-video.h/24;
    for(int i=0;i<subtitle_lines;i++){
        SDL_Texture *texture=subtitle_textures[i];if(!texture||!subtitle_heights[i])continue;
        int h=line_height,w=subtitle_widths[i]*h/subtitle_heights[i];
        if(w>video.w*9/10){w=video.w*9/10;h=w*subtitle_heights[i]/subtitle_widths[i];}
        SDL_Rect dst={video.x+(video.w-w)/2,bottom-(subtitle_lines-i)*line_height,w,h};
        SDL_SetTextureColorMod(texture,0,0,0);
        for(int dy=-1;dy<=1;dy+=2)for(int dx=-1;dx<=1;dx+=2){SDL_Rect shadow=dst;shadow.x+=dx;shadow.y+=dy;SDL_RenderCopy(renderer,texture,NULL,&shadow);}
        SDL_SetTextureColorMod(texture,255,255,255);SDL_RenderCopy(renderer,texture,NULL,&dst);
    }
}
