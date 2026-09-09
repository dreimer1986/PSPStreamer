/* SPDX-License-Identifier: GPL-2.0-or-later
 * PSP adapter, no display-mode changes, no audio/ME calls. */
#include "milkdrop_warp.h"
#include "milkdrop_preset.h"
#include "milkdrop_wave.h"
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
static int md_tv_top = 86, md_last_top;
static unsigned long long md_origin, md_next;
static MdSignalState md_signal_state;
static int md_signal_active;
static short md_right[MD_WAVE_SAMPLES];
void md_set_tv_title_bottom(int bottom) {
    int top = bottom + 5;
    md_tv_top = top < 70 ? 70 : top > 102 ? 102 : top;
}

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
    md_signal_reset(&md_signal_state); md_signal_active = 0;
    memset(md_right,0,sizeof(md_right));
    md_wave_forget();
    return 1;
}
void md_stop(void) {
    md_wave_capture=0;
    if (!md_list) return;
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
    sceGuTerm();
    free(md_list); md_list = NULL;
}
int md_frame(int tv, int fullscreen, const unsigned char bands[12], int level,
              unsigned long long now, int preset) {
    MdVertex *mesh, *ring, *blit;
    int target = 1-md_front;
    /* Independent edges: changing top/left must not move bottom/right.
     * TV begins just inside the blue border and five rows below title ink. */
    int left = fullscreen ? 0 : tv ? 26 : 38;
    int top = fullscreen ? 0 : tv ? md_tv_top : 74;
    int right = fullscreen ? (tv ? 720 : 480) : tv ? 534 : 344;
    int bottom = fullscreen ? (tv ? 480 : 272) : tv ? 294 : 149;
    int width = right - left, height = bottom - top;
    float seconds;
    MdPreset evaluated;
    unsigned int custom_color = 0;
    unsigned long long finished, cost;
    if (!md_list || preset < 0 || preset > 3) return 0;
    if (now < md_next && tv == md_last_tv && fullscreen == md_last_fullscreen &&
        top == md_last_top) return 1;
    if (!md_origin) md_origin = now;
    seconds = (float)(now-md_origin)/1000000;
    if (preset == 3) {
        if (!md_signal_active) md_signal_reset(&md_signal_state);
        md_signal_active = 1;
        md_signal_update(&md_signal_state, bands, level, now);
        if (md_eval_preset_signal(&md_custom_preset, seconds, &md_signal_state.signal,
                                  &evaluated, &custom_color, &md_runtime_error) != MD_FILE_OK)
            return -1; /* No GU list was started; caller retains music playback. */
    } else md_signal_active = 0;
    int circular=preset==3 && md_custom_preset.wave_mode==0;
    md_wave_capture=circular;
    if(circular) {
        if(level>0) md_wave_snapshot(md_right);
        else memset(md_right,0,sizeof(md_right));
    }
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
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    int wrap=preset==3 && !md_custom_preset.wrap ? GU_CLAMP : GU_REPEAT;
    sceGuTexWrap(wrap,wrap);
    sceGuTexScale(1, 1); sceGuTexOffset(0, 0);
    sceGuTexFlush();
    mesh = sceGuGetMemory(MD_MESH_VERTICES*sizeof(*mesh));
    md_warp_mesh(mesh, preset == 3 ? &evaluated : &md_presets[preset], seconds);
    sceGuDrawArray(GU_TRIANGLES, MD_FORMAT, MD_MESH_VERTICES, NULL, mesh);
    sceGuDisable(GU_TEXTURE_2D);
    if(circular) {
        ring=sceGuGetMemory(MD_WAVE_VERTICES*sizeof(*ring));
        unsigned int alpha=(unsigned int)(md_custom_preset.wave_alpha*255);
        md_wave_circle(ring,md_right,md_custom_preset.wave_scale,md_custom_preset.wave_smoothing,
                       seconds,(float)height/width,(custom_color&0xffffff)|(alpha<<24));
        sceGuEnable(GU_BLEND);
        sceGuBlendFunc(GU_ADD,GU_SRC_ALPHA,GU_ONE_MINUS_SRC_ALPHA,0,0);
        sceGuDrawArray(GU_LINE_STRIP,MD_FORMAT,MD_WAVE_VERTICES,NULL,ring);
        sceGuDisable(GU_BLEND);
    } else if (level > 0) {
        ring = sceGuGetMemory(97*sizeof(*ring));
        md_audio_ring(ring, bands, level, seconds, preset);
        if (preset == 3) {
            for (int i = 0; i < 97; i++) ring[i].color = custom_color;
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
    float gamma=preset==3 ? md_custom_preset.gamma : 1;
    for(int pass=0;pass<(int)(gamma+.999f);pass++) {
      float strength=gamma-pass; if(strength>1) strength=1;
      unsigned int shade=(unsigned int)(strength*255);
      unsigned int tint=0xff000000U|shade|(shade<<8)|(shade<<16);
      if(pass) { sceGuEnable(GU_BLEND); sceGuBlendFunc(GU_ADD,GU_FIX,GU_FIX,0xffffff,0xffffff); }
      for (int x = 0; x < width; x += 32) {
        int end = x+32 < width ? x+32 : width;
        blit = sceGuGetMemory(2*sizeof(*blit));
        blit[0] = (MdVertex){(float)x*MD_TEXTURE/width, 0, tint,
                              (float)(left+x), (float)top, 0};
        blit[1] = (MdVertex){(float)end*MD_TEXTURE/width, MD_TEXTURE, tint,
                              (float)(left+end), (float)(top+height), 0};
        sceGuDrawArray(GU_SPRITES, MD_FORMAT, 2, NULL, blit);
    }
    }
    sceGuDisable(GU_BLEND);
    sceGuFinish();
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
    md_front = target;
    md_last_tv = tv; md_last_fullscreen = fullscreen;
    md_last_top = top;
    finished = sceKernelGetSystemTimeWide();
    cost = finished - now;
    /* Never attempt catch-up frames. Expensive frames lower the visual rate,
     * rather than changing audio clocks or consuming a whole CPU core. */
    md_next = finished + (cost > 16666ULL ? cost*3 : 50000ULL);
    return 1;
}
