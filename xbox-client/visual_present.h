#include "visual_scale.h"
static int visual_present_pixels(const uint32_t *pixels,int stride,int sw,int sh,
                                  SDL_Rect logical,int abgr){
    SDL_Surface *surface=SDL_GetWindowSurface(window);
    if(!surface||surface->format->BytesPerPixel!=4||surface->format->Rmask!=0xff0000||surface->format->Bmask!=255)return 0;
    SDL_Rect dst={logical.x*width/720,logical.y*height/480,
        (logical.x+logical.w)*width/720-logical.x*width/720,
        (logical.y+logical.h)*height/480-logical.y*height/480};
    if(dst.x<0||dst.y<0||dst.x+dst.w>surface->w||dst.y+dst.h>surface->h)return 0;
    if(SDL_RenderFlush(renderer)<0||SDL_LockSurface(surface)<0)return 0;
    int ok=xbox_scale_opaque((uint32_t*)((unsigned char*)surface->pixels+dst.y*surface->pitch)+dst.x,
        surface->pitch/4,dst.w,dst.h,pixels,stride,sw,sh,abgr);
    SDL_UnlockSurface(surface);return ok;
}
