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

static void PSPX_DestroyWindow(_THIS, SDL_Window *window)
{
    (void)_this;
    if (xbox_window == window)
        xbox_window = NULL;
}

static SDL_VideoDevice *PSPX_CreateDevice(int index)
{
    SDL_VideoDevice *device = XBOX_CreateDevice(index);
    if (device)
        device->DestroyWindow = PSPX_DestroyWindow;
    return device;
}

VideoBootStrap XBOX_bootstrap = {
    XBOXVID_DRIVER_NAME, "SDL Xbox with window lifecycle repair",
    XBOX_Available, PSPX_CreateDevice
};
