/* SPDX-License-Identifier: GPL-2.0-or-later
 * Bounded LRU, one renderer/font owner. Clear before either is destroyed.
 */
#define XBOX_TEXT_CACHE_ENTRIES 96
#define XBOX_TEXT_CACHE_BYTES (2u*1024*1024)
static struct XboxText {
    char *text;SDL_Color color;SDL_Texture *texture;int width,height;
    unsigned bytes;Uint64 age;
} text_cache[XBOX_TEXT_CACHE_ENTRIES];
static unsigned text_cache_bytes;
static Uint64 text_cache_age;
static void text_cache_remove(unsigned i){
    struct XboxText *t=&text_cache[i];
    if(t->texture)SDL_DestroyTexture(t->texture);
    free(t->text);text_cache_bytes-=t->bytes;memset(t,0,sizeof(*t));
}
static void text_cache_clear(void){
    for(unsigned i=0;i<XBOX_TEXT_CACHE_ENTRIES;i++)text_cache_remove(i);
    text_cache_age=0;
}
static unsigned text_cache_oldest(void){
    unsigned oldest=0;
    for(unsigned i=0;i<XBOX_TEXT_CACHE_ENTRIES;i++){
        if(!text_cache[i].texture)return i;
        if(text_cache[i].age<text_cache[oldest].age)oldest=i;
    }
    return oldest;
}
static void text_cached_draw(SDL_Renderer *renderer,TTF_Font *font,const char *s,
                             int x,int y,int max_width,SDL_Color color){
    if(!*s||max_width<=0)return;
    for(unsigned i=0;i<XBOX_TEXT_CACHE_ENTRIES;i++){
        struct XboxText *t=&text_cache[i];
        if(t->texture&&!memcmp(&t->color,&color,sizeof(color))&&!strcmp(t->text,s)){
            t->age=++text_cache_age;
            SDL_Rect src={0,0,t->width<max_width?t->width:max_width,t->height},dst={x,y,src.w,src.h};
            SDL_RenderCopy(renderer,t->texture,&src,&dst);return;
        }
    }
    SDL_Surface *surface=TTF_RenderUTF8_Blended(font,s,color);if(!surface)return;
    SDL_Texture *texture=SDL_CreateTextureFromSurface(renderer,surface);
    unsigned bytes=(unsigned)surface->w*surface->h*4;int w=surface->w,h=surface->h;
    SDL_FreeSurface(surface);if(!texture)return;
    size_t len=strlen(s);int cached=0;
    if(bytes<=XBOX_TEXT_CACHE_BYTES&&len<=1024){
        /* Empty slots don't release bytes: select a populated LRU for budget eviction. */
        while(text_cache_bytes+bytes>XBOX_TEXT_CACHE_BYTES){
            unsigned oldest=XBOX_TEXT_CACHE_ENTRIES;
            for(unsigned i=0;i<XBOX_TEXT_CACHE_ENTRIES;i++)if(text_cache[i].texture&&
                (oldest==XBOX_TEXT_CACHE_ENTRIES||text_cache[i].age<text_cache[oldest].age))oldest=i;
            if(oldest==XBOX_TEXT_CACHE_ENTRIES)break;
            text_cache_remove(oldest);
        }
        unsigned i=text_cache_oldest();char *key=malloc(len+1);
        if(key){
            memcpy(key,s,len+1);text_cache_remove(i);
            text_cache[i]=(struct XboxText){key,color,texture,w,h,bytes,++text_cache_age};
            text_cache_bytes+=bytes;cached=1;
        }
    }
    SDL_Rect src={0,0,w<max_width?w:max_width,h},dst={x,y,src.w,src.h};
    SDL_RenderCopy(renderer,texture,&src,&dst);
    if(!cached)SDL_DestroyTexture(texture);
}
