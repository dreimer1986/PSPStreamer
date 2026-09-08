/* SPDX-License-Identifier: GPL-2.0-or-later
 * PSP adapter, no display-mode changes, no audio/ME calls. */
#include "milkdrop_warp.h"
#include "milkdrop_preset.h"
#include <pspgu.h>
#include <pspge.h>
#include <pspkernel.h>
#include <malloc.h>
#include <string.h>
#include <stdint.h>

/* Even native TV scanout ends before these textures. Real 2 MiB EDRAM only. */
#define MD_TEXTURE_BYTES (MD_TEXTURE * MD_TEXTURE * 4)
#define MD_TEXTURE_BASE (768 * 480 * 4)
#define MD_LIST_BYTES 65536
#define MD_FORMAT (GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D)
static unsigned int *md_list;
static int md_front;
static int md_last_tv, md_last_fullscreen;
static unsigned long long md_origin, md_next;

static void *md_texture(int index) {
    return (void *)(uintptr_t)(0x04000000 + MD_TEXTURE_BASE + index*MD_TEXTURE_BYTES);
}
static void md_target(int offset, int stride, int width, int height) {
    sceGuDrawBufferList(GU_PSM_8888, (void *)(uintptr_t)offset, stride);
    sceGuOffset(2048-width/2, 2048-height/2);
    sceGuViewport(2048, 2048, width, height);
    sceGuScissor(0, 0, width, height);
}
int md_start(void) {
    if (md_list) return 1;
    if (MD_TEXTURE_BASE + 2*MD_TEXTURE_BYTES > sceGeEdramGetSize()) return 0;
    md_list = memalign(64, MD_LIST_BYTES);
    if (!md_list) return 0;
    memset((void *)(uintptr_t)(0x44000000 + MD_TEXTURE_BASE), 0, 2*MD_TEXTURE_BYTES);
    if (sceGuInit() < 0) {
        free(md_list); md_list = NULL;
        return 0;
    }
    md_front = 0; md_origin = md_next = 0;
    md_last_tv = md_last_fullscreen = -1;
    return 1;
}
void md_stop(void) {
    if (!md_list) return;
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
    sceGuTerm();
    free(md_list); md_list = NULL;
}
int md_frame(int tv, int fullscreen, const unsigned char bands[12], int level,
              unsigned long long now, int preset) {
    MdVertex *mesh, *ring, *blit;
    int target = 1-md_front;
    /* Receiver aperture: calibrated edges, independent of full scanout.
     * LCD [38,344) x [82,149); TV [41,531) x [108,293). */
    int left = tv ? 41 : 38, top = tv ? 108 : 82;
    int width = tv ? (fullscreen ? 720 : 490) : (fullscreen ? 480 : 306);
    int height = tv ? (fullscreen ? 480 : 185) : (fullscreen ? 272 : 67);
    float seconds;
    unsigned long long finished, cost;
    if (!md_list || preset < 0 || preset > 3) return 0;
    if (now < md_next && tv == md_last_tv && fullscreen == md_last_fullscreen) return 1;
    if (!md_origin) md_origin = now;
    seconds = (float)(now-md_origin)/1000000;
    if (fullscreen) left = top = 0;
    if (sceGuStart(GU_DIRECT, md_list) < 0) { md_stop(); return 0; }
    sceGuDisable(GU_DEPTH_TEST); sceGuDisable(GU_CULL_FACE);
    sceGuDisable(GU_LIGHTING); sceGuDisable(GU_BLEND);
    sceGuDisable(GU_ALPHA_TEST); sceGuDisable(GU_STENCIL_TEST);
    sceGuEnable(GU_SCISSOR_TEST); sceGuEnable(GU_TEXTURE_2D);
    md_target(MD_TEXTURE_BASE + target*MD_TEXTURE_BYTES, MD_TEXTURE, MD_TEXTURE, MD_TEXTURE);
    sceGuTexMode(GU_PSM_8888, 0, 0, 0);
    sceGuTexImage(0, MD_TEXTURE, MD_TEXTURE, MD_TEXTURE, md_texture(md_front));
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR); sceGuTexWrap(GU_REPEAT, GU_REPEAT);
    sceGuTexScale(1, 1); sceGuTexOffset(0, 0);
    sceGuTexFlush();
    mesh = sceGuGetMemory(MD_MESH_VERTICES*sizeof(*mesh));
    md_warp_mesh(mesh, preset == 3 ? &md_custom_preset.warp : &md_presets[preset], seconds);
    sceGuDrawArray(GU_TRIANGLES, MD_FORMAT, MD_MESH_VERTICES, NULL, mesh);
    sceGuDisable(GU_TEXTURE_2D);
    if (level > 0) {
        ring = sceGuGetMemory(97*sizeof(*ring));
        md_audio_ring(ring, bands, level, seconds, preset);
        if (preset == 3) {
            unsigned int color = 0xff000000U | (unsigned int)(md_custom_preset.red*255) |
                ((unsigned int)(md_custom_preset.green*255)<<8) |
                ((unsigned int)(md_custom_preset.blue*255)<<16);
            for (int i = 0; i < 97; i++) ring[i].color = color;
        }
        sceGuDrawArray(GU_LINE_STRIP, MD_FORMAT, 97, NULL, ring);
    }
    sceGuTexSync();
    md_target(0, tv ? 768 : 512, tv ? 720 : 480, tv ? 480 : 272);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexImage(0, MD_TEXTURE, MD_TEXTURE, MD_TEXTURE, md_texture(target));
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuTexFlush();
    /* Slice the final stretch into narrow sprites, as recommended for PSP
     * texture-cache locality. It never copies the full scanout on the CPU. */
    for (int x = 0; x < width; x += 32) {
        int end = x+32 < width ? x+32 : width;
        blit = sceGuGetMemory(2*sizeof(*blit));
        blit[0] = (MdVertex){(float)x*MD_TEXTURE/width, 0, 0xffffffff,
                              (float)(left+x), (float)top, 0};
        blit[1] = (MdVertex){(float)end*MD_TEXTURE/width, MD_TEXTURE, 0xffffffff,
                              (float)(left+end), (float)(top+height), 0};
        sceGuDrawArray(GU_SPRITES, MD_FORMAT, 2, NULL, blit);
    }
    sceGuFinish();
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
    md_front = target;
    md_last_tv = tv; md_last_fullscreen = fullscreen;
    finished = sceKernelGetSystemTimeWide();
    cost = finished - now;
    /* Never attempt catch-up frames. Expensive frames lower the visual rate,
     * rather than changing audio clocks or consuming a whole CPU core. */
    md_next = finished + (cost > 16666ULL ? cost*3 : 50000ULL);
    return 1;
}
