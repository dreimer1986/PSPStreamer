/* SPDX-License-Identifier: GPL-2.0-or-later
 * Narrow adapter for nxdk 14d5ee97 SDL: its singleton survives DestroyWindow
 * and VideoQuit. Re-creating a window then fails with "Xbox only supports
 * one window". Compile the pinned upstream driver unchanged and install the
 * missing destructor. The SDK checkout itself is not patched.
 * Upstream source retains its zlib license and copyright notices.
 */
#define XBOX_bootstrap XBOX_original_bootstrap
#include "SDL_xbvideo.c"
#undef XBOX_bootstrap
#include "framebuffer_copy.h"

Uint64 xbox_fb_copy_bytes;
unsigned xbox_fb_copy_calls;
static int PSPX_UpdateWindowFramebuffer(_THIS,SDL_Window *window,const SDL_Rect *rects,int count)
{
    (void)_this;
    SDL_Surface *s=SDL_GetWindowData(window,"_SDL_XboxSurface");
    if(!s)return SDL_SetError("Missing Xbox window surface");
    VIDEO_MODE mode=XVideoGetMode();
    int result=xbox_copy_damage(s,XVideoGetFB(),mode.width,mode.height,
                               pixelFormatSelector(mode.bpp),mode.width*(mode.bpp/8),
                               rects,count,&xbox_fb_copy_bytes);
    XVideoFlushFB();xbox_fb_copy_calls++;
    return result;
}

static void PSPX_DestroyWindow(_THIS, SDL_Window *window)
{
    (void)_this;
    if (xbox_window == window)
        xbox_window = NULL;
}

static SDL_VideoDevice *PSPX_CreateDevice(int index)
{
    SDL_VideoDevice *device = XBOX_CreateDevice(index);
    if (device){
        device->DestroyWindow = PSPX_DestroyWindow;
        device->UpdateWindowFramebuffer=PSPX_UpdateWindowFramebuffer;
    }
    return device;
}

VideoBootStrap XBOX_bootstrap = {
    XBOXVID_DRIVER_NAME, "SDL Xbox with window lifecycle repair",
    XBOX_Available, PSPX_CreateDevice
};
