/* Build against the EXACT pinned nxdk SDL source, with its dummy host driver. */
#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include "../xbox-client/render_clip.h"
static SDL_AssertState failed(const SDL_AssertData *data,void *unused){
    (void)unused;fprintf(stderr,"Assertion: %s\n",data->condition);exit(42);
}
int main(int argc,char **argv){
    (void)argv;SDL_SetHint(SDL_HINT_RENDER_BATCHING,"1");
    if(SDL_Init(SDL_INIT_VIDEO))return 1;
    SDL_SetAssertionHandler(failed,NULL);
    SDL_Window *w=SDL_CreateWindow("test",0,0,640,480,0);
    SDL_Renderer *r=SDL_CreateRenderer(w,-1,SDL_RENDERER_SOFTWARE);if(!r)return 2;
    SDL_Texture *t=SDL_CreateTexture(r,SDL_PIXELFORMAT_ARGB8888,SDL_TEXTUREACCESS_STATIC,10,10);
    SDL_RenderCopy(r,t,NULL,NULL);SDL_DestroyTexture(t); /* flush like text_at */
    SDL_Rect clip={0,0,100,100};
    if(argc>1)SDL_RenderSetClipRect(r,&clip);else xbox_render_clip(r,&clip);
    SDL_SetRenderDrawColor(r,255,0,0,255);SDL_RenderFillRect(r,NULL);
    SDL_RenderPresent(r);SDL_DestroyRenderer(r);SDL_DestroyWindow(w);SDL_Quit();
    puts("Clip after texture destruction: passed");return 0;
}
