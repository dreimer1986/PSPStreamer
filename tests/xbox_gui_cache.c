/* Targeted host checks for the actual Xbox damage copy and bounded text cache. */
#include <SDL.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef void TTF_Font;
static unsigned rasterizations;
static SDL_Surface *TTF_RenderUTF8_Blended(TTF_Font *font,const char *text,SDL_Color color){
    (void)font;rasterizations++;
    SDL_Surface *s=SDL_CreateRGBSurfaceWithFormat(0,(int)strlen(text)*8,16,32,SDL_PIXELFORMAT_ARGB8888);
    if(s)SDL_FillRect(s,NULL,SDL_MapRGBA(s->format,color.r,color.g,color.b,color.a));
    return s;
}
#include "../xbox-client/framebuffer_copy.h"
#include "../xbox-client/text_cache.h"
int main(void){
    SDL_Surface *s=SDL_CreateRGBSurfaceWithFormat(0,64,48,32,SDL_PIXELFORMAT_ARGB8888);
    assert(s);SDL_FillRect(s,NULL,0xff123456);
    unsigned char output[64*48*4+32];memset(output,0xa5,sizeof(output));
    Uint64 bytes=0;SDL_Rect r={10,20,30,3};
    assert(!xbox_copy_damage(s,output,64,48,s->format->format,256,&r,1,&bytes));
    assert(bytes==360);
    for(int y=0;y<48;y++)for(int x=0;x<64;x++){
        Uint32 actual;memcpy(&actual,output+y*256+x*4,4);
        assert(actual==((y>=20&&y<23&&x>=10&&x<40)?0xff123456:0xa5a5a5a5));
    }
    r=(SDL_Rect){-2,46,5,7};assert(!xbox_copy_damage(s,output,64,48,s->format->format,256,&r,1,&bytes));
    assert(bytes==384);
    for(unsigned i=64*48*4;i<sizeof(output);i++)assert(output[i]==0xa5);
    r=(SDL_Rect){0,0,64,48};bytes=0;
    assert(!xbox_copy_damage(s,output,64,48,SDL_PIXELFORMAT_RGB565,128,&r,1,&bytes));assert(bytes==6144);
    Uint16 pixel;memcpy(&pixel,output,2);assert(pixel==0x11aa);
    SDL_Renderer *renderer=SDL_CreateSoftwareRenderer(s);assert(renderer);
    SDL_Color white={255,255,255,255};
    text_cached_draw(renderer,NULL,"Grüße",0,0,60,white);
    text_cached_draw(renderer,NULL,"Grüße",2,3,20,white);
    assert(rasterizations==1&&text_cache_bytes>0);
    text_cached_draw(renderer,NULL,"Grüße",0,0,60,(SDL_Color){255,0,0,255});assert(rasterizations==2);
    for(int i=0;i<130;i++){char label[32];snprintf(label,sizeof(label),"entry %d",i);text_cached_draw(renderer,NULL,label,0,0,60,white);}
    assert(text_cache_bytes<=XBOX_TEXT_CACHE_BYTES);
    /* Large strings exercise the byte limit rather than just the entry limit. */
    char large[1001];memset(large,'x',1000);large[1000]=0;
    for(int i=0;i<12;i++){large[0]='a'+i;text_cached_draw(renderer,NULL,large,0,0,60,white);assert(text_cache_bytes<=XBOX_TEXT_CACHE_BYTES);}
    SDL_RenderFlush(renderer);text_cache_clear();assert(text_cache_bytes==0);
    for(int i=0;i<XBOX_TEXT_CACHE_ENTRIES;i++)assert(!text_cache[i].texture&&!text_cache[i].text);
    SDL_DestroyRenderer(renderer);SDL_FreeSurface(s);SDL_Quit();
    puts("PASS: partial copy, clipping, format conversion, guards, UTF8 cache hits, LRU and byte bounds, cleanup");
}
