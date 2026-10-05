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
#include "pbkit.c"
