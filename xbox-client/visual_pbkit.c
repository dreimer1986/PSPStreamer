/* SPDX-License-Identifier: GPL-2.0-or-later
 * Compile the pinned nxdk pbkit with offscreen-only buffers. No SDK edits,
 * EEPROM writes, display-mode switches or additional HD scanout buffers.
 * pbkit otherwise allocates THREE full-resolution targets plus depth.
 * Its SDL/debug screen remains the sole scanout owner.
 */
#include <hal/video.h>
static VIDEO_MODE visual_offscreen_mode(void) {
    VIDEO_MODE mode=XVideoGetMode();mode.width=512;mode.height=512;return mode;
}
#define XVideoGetMode visual_offscreen_mode
#define pb_kill visual_pb_kill_inner
#include "pbkit.c"
#undef pb_kill
#undef XVideoGetMode

void pb_kill(void) {
    visual_pb_kill_inner();
    /* Return scanout ownership explicitly to HAL/SDL, including encoder enable.
     * No framebuffer allocation or video-mode change during media teardown. */
    XVideoSetFB(XVideoGetFB());
    XVideoSetVideoEnable(TRUE);
    XVideoFlushFB();
}
