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
#define MD_WIDTH 512
#define MD_HEIGHT 256
static int md_texture_base, md_texture_bytes, md_pixel_format;
#define MD_TEXTURE_BYTES md_texture_bytes
#define MD_TEXTURE_BASE md_texture_base
#define MD_LIST_BYTES 65536
#define MD_FORMAT (GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D)
static unsigned int *md_list;
static int md_front;
static int md_last_tv, md_last_fullscreen;
static int md_tv_top = 86, md_last_top;
static unsigned long long md_origin, md_next;
static MdSignalState md_signal_state;
static MdPresetState md_preset_state;
static int md_signal_active;
static short md_right[MD_WAVE_SAMPLES];
/* Geometry helpers retain their logical 256-square coordinate system. Only
 * this adapter maps it to the rectangular physical feedback surface. */
static void md_expand(MdVertex *v, int count, int half_texel) {
    for (int i=0;i<count;i++) {
        v[i].x *= 2;
        v[i].u = half_texel ? (v[i].u-.5f)*2+.5f : v[i].u*2;
    }
}
void md_set_tv_title_bottom(int bottom) {
    int top = bottom + 5;
    md_tv_top = top < 70 ? 70 : top > 102 ? 102 : top;
}

static void *md_texture(int index) {
    return (void *)(uintptr_t)(0x04000000 + MD_TEXTURE_BASE + index*MD_TEXTURE_BYTES);
}
static void md_target(int offset, int stride, int width, int height) {
    sceGuDrawBufferList(offset ? md_pixel_format : GU_PSM_8888, (void *)(uintptr_t)offset, stride);
    sceGuOffset(2048-width/2, 2048-height/2);
    sceGuViewport(2048, 2048, width, height);
    sceGuScissor(0, 0, width, height);
}
#include "milkdrop_decor_gu.h"
int md_start(void) {
    if (md_list) return 1;
    if (sceGeEdramGetSize() < 2*1024*1024) return 0;
    md_list = memalign(64, MD_LIST_BYTES);
    if (!md_list) return 0;
    if (sceGuInit() < 0) {
        free(md_list); md_list = NULL;
        return 0;
    }
    md_front = 0; md_origin = md_next = 0;
    md_last_tv = md_last_fullscreen = -1;
    md_signal_reset(&md_signal_state); md_signal_active = 0;
    memset(&md_preset_state,0,sizeof(md_preset_state));
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
    MdVertex *mesh, *ring;
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
    MdDecor frame_decor;
    unsigned int custom_color = 0;
    unsigned long long finished, cost;
    if (!md_list || preset < 0 || preset > 3) return 0;
    if (now < md_next && tv == md_last_tv && fullscreen == md_last_fullscreen &&
        top == md_last_top) return 1;
    if (tv != md_last_tv) {
        md_texture_base = tv ? 768*480*4 : 512*272*4;
        md_texture_bytes = MD_WIDTH*MD_HEIGHT*(tv ? 2 : 4);
        md_pixel_format = tv ? GU_PSM_5650 : GU_PSM_8888;
        if ((unsigned int)(MD_TEXTURE_BASE + 2*MD_TEXTURE_BYTES) > sceGeEdramGetSize()) return 0;
        memset((void *)(uintptr_t)(0x44000000 + MD_TEXTURE_BASE), 0, 2*MD_TEXTURE_BYTES);
        md_front=0; target=1;
    }
    if (!md_origin) md_origin = now;
    seconds = (float)(now-md_origin)/1000000;
    if (preset == 3) {
        if (!md_signal_active) {
            md_signal_reset(&md_signal_state);
            memset(&md_preset_state,0,sizeof(md_preset_state));
        }
        md_signal_active = 1;
        md_signal_update(&md_signal_state, bands, level, now);
        if (md_eval_preset_state(&md_custom_preset, seconds, &md_signal_state.signal,
                                  &md_preset_state, &evaluated, &custom_color, &frame_decor, &md_runtime_error) != MD_FILE_OK)
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
    md_target(MD_TEXTURE_BASE + target*MD_TEXTURE_BYTES, MD_WIDTH, MD_WIDTH, MD_HEIGHT);
    sceGuTexMode(md_pixel_format, 0, 0, 0);
    sceGuTexImage(0, MD_WIDTH, MD_HEIGHT, MD_WIDTH, md_texture(md_front));
    sceGuTexFunc(GU_TFX_MODULATE, GU_TCC_RGBA);
    sceGuTexFilter(GU_LINEAR, GU_LINEAR);
    int wrap=preset==3 && !md_custom_preset.wrap ? GU_CLAMP : GU_REPEAT;
    sceGuTexWrap(wrap,wrap);
    sceGuTexScale(1, 1); sceGuTexOffset(0, 0);
    sceGuTexFlush();
    mesh = sceGuGetMemory(MD_MESH_VERTICES*sizeof(*mesh));
    md_warp_mesh(mesh, preset == 3 ? &evaluated : &md_presets[preset], seconds);
    md_expand(mesh, MD_MESH_VERTICES, 1);
    sceGuDrawArray(GU_TRIANGLES, MD_FORMAT, MD_MESH_VERTICES, NULL, mesh);
    sceGuDisable(GU_TEXTURE_2D);
    if(preset==3) md_shapes(&frame_decor,(float)height/width);
    if(circular) {
        const MdDecor *d=&frame_decor;
        ring=sceGuGetMemory(MD_WAVE_VERTICES*sizeof(*ring));
        float alpha=d->wave_alpha;
        if(d->wave_mod_alpha) {
            float relative=(md_signal_state.signal.values[7]+md_signal_state.signal.values[8]+
                            md_signal_state.signal.values[9])/3;
            float mix=(relative-d->wave_mod_start)/(d->wave_mod_end-d->wave_mod_start);
            if(mix<0) mix=0;
            if(mix>1) mix=1;
            alpha*=mix;
        }
        float r=(custom_color&255)/255.0f,g=((custom_color>>8)&255)/255.0f,b=((custom_color>>16)&255)/255.0f;
        if(d->wave_brighten) {float peak=r>g?r:g; if(b>peak) peak=b; if(peak>0) {r/=peak;g/=peak;b/=peak;}}
        md_wave_circle_style(ring,md_right,md_custom_preset.wave_scale,md_custom_preset.wave_smoothing,
                       seconds,(float)height/width,md_rgba(r,g,b,alpha),d);
        md_expand(ring, MD_WAVE_VERTICES, 0);
        md_blend(d->wave_additive!=0);
        int primitive=d->wave_dots?GU_POINTS:GU_LINE_STRIP;
        sceGuDrawArray(primitive,MD_FORMAT,MD_WAVE_VERTICES,NULL,ring);
        if(d->wave_thick) for(int pass=0;pass<3;pass++) {
            MdVertex *copy=sceGuGetMemory(MD_WAVE_VERTICES*sizeof(*copy));
            for(int i=0;i<MD_WAVE_VERTICES;i++) {
                copy[i]=ring[i];
                copy[i].x+=pass<2?1:0; copy[i].y+=pass>0?1:0;
            }
            sceGuDrawArray(primitive,MD_FORMAT,MD_WAVE_VERTICES,NULL,copy);
        }
        sceGuDisable(GU_BLEND);
    } else if (level > 0) {
        ring = sceGuGetMemory(97*sizeof(*ring));
        md_audio_ring(ring, bands, level, seconds, preset);
        md_expand(ring, 97, 0);
        if (preset == 3) {
            for (int i = 0; i < 97; i++) ring[i].color = custom_color;
        }
        sceGuDrawArray(GU_LINE_STRIP, MD_FORMAT, 97, NULL, ring);
    }
    if(preset==3) {
        md_border(&frame_decor.outer,0);
        md_border(&frame_decor.inner,frame_decor.outer.size);
    }
    sceGuTexSync();
    /* The old feedback is no longer needed. Compose echo/gamma into that
     * surface, keeping the new RAW feedback intact for the next frame.
     * Never expose the dark base or intermediate additive passes on screen. */
    md_target(MD_TEXTURE_BASE + md_front*MD_TEXTURE_BYTES, MD_WIDTH, MD_WIDTH, MD_HEIGHT);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexImage(0, MD_WIDTH, MD_HEIGHT, MD_WIDTH, md_texture(target));
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuTexFlush();
    const MdDecor *d=&frame_decor;
    md_present(0,0,MD_WIDTH,MD_HEIGHT,preset==3?d->gamma:1,
        preset==3?d->echo_zoom:1,preset==3?d->echo_alpha:0,preset==3?(int)d->echo_orient:0);
    sceGuTexSync();
    md_target(0, tv ? 768 : 512, tv ? 720 : 480, tv ? 480 : 272);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexImage(0, MD_WIDTH, MD_HEIGHT, MD_WIDTH, md_texture(md_front));
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuTexFlush();
    /* Slice the final stretch into narrow sprites, as recommended for PSP
     * texture-cache locality. It never copies the full scanout on the CPU. */
    md_present(left,top,width,height,1,1,0,0);
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
