/* Test the actual pinned Xbox SDL driver + app adapter; only hardware and
 * unrelated SDL services are stubbed. No console output is simulated. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#define SDL_VIDEO_DRIVER_XBOX 1
#define SDL_SetMouseFocus test_SetMouseFocus
#define SDL_SetKeyboardFocus test_SetKeyboardFocus
#define SDL_AddBasicVideoDisplay test_AddBasicVideoDisplay
#define SDL_AddDisplayMode test_AddDisplayMode
#include "../xbox-client/video_backend.c"

static VIDEO_MODE mode={640,480,32,60};
VIDEO_MODE XVideoGetMode(void){return mode;}
void *XVideoGetFB(void){return NULL;}
void XVideoFlushFB(void){}
void SDL_SetMouseFocus(SDL_Window *w){(void)w;}
void SDL_SetKeyboardFocus(SDL_Window *w){(void)w;}
int SDL_AddBasicVideoDisplay(const SDL_DisplayMode *m){(void)m;return 0;}
SDL_bool SDL_AddDisplayMode(SDL_VideoDisplay *d,const SDL_DisplayMode *m){(void)d;(void)m;return SDL_TRUE;}
void XBOX_PumpEvents(_THIS){(void)_this;}
int SDL_XBOX_CreateWindowFramebuffer(_THIS,SDL_Window *w,Uint32 *f,void **p,int *s){(void)_this;(void)w;(void)f;(void)p;(void)s;return 0;}
int SDL_XBOX_UpdateWindowFramebuffer(_THIS,SDL_Window *w,const SDL_Rect *r,int n){(void)_this;(void)w;(void)r;(void)n;return 0;}
void SDL_XBOX_DestroyWindowFramebuffer(_THIS,SDL_Window *w){(void)_this;(void)w;}

int main(void){
    SDL_Window first={0},second={0};
    SDL_VideoDevice *old=XBOX_original_bootstrap.create(0);
    if(!old||old->CreateSDLWindow(old,&first)||old->DestroyWindow)return 1;
    /* Upstream VideoQuit/DeleteDevice leaves a dangling singleton. */
    old->VideoQuit(old);old->free(old);
    old=XBOX_original_bootstrap.create(0);
    if(old->CreateSDLWindow(old,&second)==0)return 2;
    old->free(old);xbox_window=NULL;
    const int sizes[][2]={{640,480},{720,480},{1280,720},{1920,1080},{640,480}};
    for(int i=0;i<5;i++){
        mode.width=sizes[i][0];mode.height=sizes[i][1];
        SDL_VideoDevice *d=XBOX_bootstrap.create(0);
        if(!d||!d->DestroyWindow||d->CreateSDLWindow(d,&first))return 3;
        if(first.w!=mode.width||first.h!=mode.height)return 4;
        /* Still reject a genuinely simultaneous second window. */
        if(d->CreateSDLWindow(d,&second)==0)return 5;
        d->DestroyWindow(d,&second);if(xbox_window!=&first)return 6;
        d->DestroyWindow(d,&first);if(xbox_window)return 7;
        d->VideoQuit(d);d->free(d);
    }
    puts("Confirmed original failure; repaired create/destroy/recreate across five modes.");return 0;
}
