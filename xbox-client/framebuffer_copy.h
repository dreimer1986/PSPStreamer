/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copy only clipped damage rectangles. Shared by the SDL adapter and tests.
 */
#include "visual_copy.h"
static int xbox_copy_damage(SDL_Surface *src,void *pixels,int width,int height,
                            Uint32 format,int pitch,const SDL_Rect *rects,int count,
                            Uint64 *bytes){
    SDL_Rect bounds={0,0,width<src->w?width:src->w,height<src->h?height:src->h};
    unsigned sb=src->format->BytesPerPixel,db=SDL_BYTESPERPIXEL(format);
    if(!pixels||!db||!sb||count<0||(!rects&&count))return -1;
    for(int i=0;i<count;i++){
        SDL_Rect r;if(!SDL_IntersectRect(&bounds,&rects[i],&r))continue;
        const Uint8 *s=(const Uint8 *)src->pixels+r.y*src->pitch+r.x*sb;
        Uint8 *d=(Uint8 *)pixels+r.y*pitch+r.x*db;
        if(src->format->format==format){
            for(int y=0;y<r.h;y++)xbox_visual_copy(d+y*pitch,s+y*src->pitch,r.w*db);
        }else if(SDL_ConvertPixels(r.w,r.h,src->format->format,s,src->pitch,format,d,pitch))return -1;
        *bytes+=(Uint64)r.w*r.h*db;
    }
    return 0;
}
