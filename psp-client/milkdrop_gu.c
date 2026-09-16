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
#define MD_LIST_BYTES 786432
#define MD_FORMAT (GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D)
static unsigned int *md_list;
static int md_front;
static int md_last_tv, md_last_fullscreen;
static int md_tv_top = 86, md_last_top;
static unsigned long long md_origin, md_next;
static MdSignalState md_signal_state;
static MdPresetState md_preset_state;
static MdPreset md_pixel_points[MD_GRID_POINTS];
static int md_signal_active;
static short md_right[MD_WAVE_SAMPLES];
static short md_left[MD_WAVE_SAMPLES];
static short md_spectrum[MD_SPECTRUM_SAMPLES];
static short md_spectrum_right[MD_SPECTRUM_SAMPLES];
static float md_bins_left[512],md_bins_right[512];
static MdWaveGeometry md_custom_geometry[MD_CUSTOM_WAVES];
static void *md_fade_image;
static unsigned long long md_fade_start;
static unsigned int md_fade_ms;
static void md_fade_clear(void) {free(md_fade_image);md_fade_image=NULL;md_fade_ms=0;}
void md_begin_preset(unsigned int fade_ms) {
    md_fade_clear();
    if(md_list && md_origin && fade_ms) {
        sceGuSync(GU_SYNC_FINISH,GU_SYNC_WHAT_DONE);
        md_fade_image=memalign(64,MD_TEXTURE_BYTES);
        if(md_fade_image) {
            memcpy(md_fade_image,(void *)(uintptr_t)(0x44000000+MD_TEXTURE_BASE+(1-md_front)*MD_TEXTURE_BYTES),MD_TEXTURE_BYTES);
            sceKernelDcacheWritebackRange(md_fade_image,MD_TEXTURE_BYTES);
            md_fade_ms=fade_ms>5000?5000:fade_ms;md_fade_start=sceKernelGetSystemTimeWide();
        }
    }
    md_origin=md_next=0;md_signal_active=0;
    memset(&md_preset_state,0,sizeof(md_preset_state));
}
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
    memset(md_left,0,sizeof(md_left));
    memset(md_spectrum,0,sizeof(md_spectrum));
    memset(md_spectrum_right,0,sizeof(md_spectrum_right));
    memset(md_bins_left,0,sizeof(md_bins_left)); memset(md_bins_right,0,sizeof(md_bins_right));
    md_wave_forget();
    return 1;
}
void md_stop(void) {
    md_wave_capture=0;
    if (!md_list) return;
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
    md_fade_clear();
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
        md_fade_clear();
        md_texture_base = tv ? 768*480*4 : 512*272*4;
        md_texture_bytes = MD_WIDTH*MD_HEIGHT*(tv ? 2 : 4);
        md_pixel_format = tv ? GU_PSM_5650 : GU_PSM_8888;
        if ((unsigned int)(MD_TEXTURE_BASE + 2*MD_TEXTURE_BYTES + MD_TEXTURE_BYTES/8) > sceGeEdramGetSize()) return 0;
        memset((void *)(uintptr_t)(0x44000000 + MD_TEXTURE_BASE), 0, 2*MD_TEXTURE_BYTES);
        md_front=0; target=1;
    }
    if (!md_origin) md_origin = now;
    seconds = (float)(now-md_origin)/1000000;
    /* Render-thread scratch: extended EEL memories must not consume the PSP
     * stack twice before entering the frame/point evaluators. */
    static MdPresetState next_state,candidate;
    next_state=md_preset_state;
    md_output_width=width;md_output_height=height;
    if (preset == 3) {
        if (!md_signal_active) {
            md_signal_reset(&md_signal_state);
            memset(&md_preset_state,0,sizeof(md_preset_state));
        }
        md_signal_active = 1;
        md_signal_update(&md_signal_state, bands, level, now);
        candidate=md_preset_state;
        if (md_eval_preset_state(&md_custom_preset, seconds, &md_signal_state.signal,
                                  &candidate, &evaluated, &custom_color, &frame_decor, &md_runtime_error) != MD_FILE_OK)
            return -1; /* No GU list was started; caller retains music playback. */
        if(md_custom_preset.pixel_program.count &&
           md_eval_pixel_grid(&md_custom_preset,&evaluated,seconds,&md_signal_state.signal,
                             &candidate,md_pixel_points,&md_runtime_error)!=MD_FILE_OK) return -1;
        next_state=candidate;
    } else md_signal_active = 0;
    int waveform=preset==3 && next_state.wave_mode>=0;
    int mode=waveform?next_state.wave_mode:0;
    int script=waveform && mode==4;
    int spiral=waveform && mode==1;
    int custom_waves=0;
    int custom_spectrum=0;
    if(preset==3) for(int i=0;i<MD_CUSTOM_WAVES;i++) custom_waves|=md_custom_preset.waves[i].enabled!=0;
    if(custom_waves) for(int i=0;i<MD_CUSTOM_WAVES;i++) custom_spectrum|=md_custom_preset.waves[i].enabled && md_custom_preset.waves[i].spectrum;
    int capture=custom_spectrum?5:custom_waves?(waveform && mode==8?4:2):waveform ? (mode==8?3:mode?2:1) : 0;
    if(capture!=md_wave_capture) {
        memset(md_right,0,sizeof(md_right)); memset(md_left,0,sizeof(md_left)); memset(md_spectrum,0,sizeof(md_spectrum));
        memset(md_bins_left,0,sizeof(md_bins_left)); memset(md_bins_right,0,sizeof(md_bins_right));
        md_wave_forget();
    }
    md_wave_capture=capture;
    if(waveform || custom_waves) {
        if(level>0) {
            if(capture==5) {
                if(md_wave_snapshot_full(md_right,md_left,md_spectrum,md_spectrum_right)) {
                    md_wave_spectrum(md_spectrum,md_bins_left); md_wave_spectrum(md_spectrum_right,md_bins_right);
                }
            } else if(capture==4) md_wave_snapshot_combined(md_right,md_left,md_spectrum);
            else if(capture==3) md_spectrum_snapshot(md_spectrum);
            else md_wave_snapshot_stereo(md_right,capture==2?md_left:NULL);
        } else { memset(md_right,0,sizeof(md_right)); memset(md_left,0,sizeof(md_left)); memset(md_spectrum,0,sizeof(md_spectrum));
            memset(md_bins_left,0,sizeof(md_bins_left)); memset(md_bins_right,0,sizeof(md_bins_right)); }
    }
    if(custom_waves && md_eval_custom_waves(&md_custom_preset,seconds,&md_signal_state.signal,
            md_right,md_left,md_bins_left,md_bins_right,&next_state,md_custom_geometry,&md_runtime_error)!=MD_FILE_OK) return -1;
    if(preset==3) md_preset_state=next_state;
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
    int wrap=preset==3 && !next_state.wrap ? GU_CLAMP : GU_REPEAT;
    sceGuTexWrap(wrap,wrap);
    sceGuTexScale(1, 1); sceGuTexOffset(0, 0);
    sceGuTexFlush();
    mesh = sceGuGetMemory(MD_MESH_VERTICES*sizeof(*mesh));
    md_warp_mesh_varying(mesh, preset == 3 ? &evaluated : &md_presets[preset],
        preset==3 && md_custom_preset.pixel_program.count?md_pixel_points:NULL, seconds);
    md_expand(mesh, MD_MESH_VERTICES, 1);
    sceGuDrawArray(GU_TRIANGLES, MD_FORMAT, MD_MESH_VERTICES, NULL, mesh);
    sceGuDisable(GU_TEXTURE_2D);
    if(preset==3 && md_preset_state.motion[0]>0) {
        MdVertex *vectors=sceGuGetMemory(16*12*2*sizeof(*vectors));
        int count=md_motion_vertices(vectors,mesh,md_preset_state.motion);
        if(count) { md_blend(0); sceGuDisable(GU_TEXTURE_2D); sceGuDrawArray(GU_LINES,MD_FORMAT,count,NULL,vectors); }
        sceGuDisable(GU_BLEND);
    }
    if(preset==3) md_shapes(&frame_decor,(float)height/width);
    if(custom_waves) for(int slot=0;slot<MD_CUSTOM_WAVES;slot++) {
        const MdCustomWave *w=&md_custom_preset.waves[slot];
        int count=md_custom_geometry[slot].count;
        if(!count) continue;
        MdVertex *vertices=sceGuGetMemory((w->dots?count:count*2-1)*sizeof(*vertices));
        if(w->dots) memcpy(vertices,md_custom_geometry[slot].vertices,count*sizeof(*vertices));
        else count=md_wave_smooth(vertices,md_custom_geometry[slot].vertices,count);
        md_expand(vertices,count,0); md_blend(w->additive!=0);
        sceGuDisable(GU_TEXTURE_2D);
        sceGuDrawArray(w->dots?GU_POINTS:GU_LINE_STRIP,MD_FORMAT,count,NULL,vertices);
        if(w->thick) for(int pass=0;pass<3;pass++) {
            MdVertex *copy=sceGuGetMemory(count*sizeof(*copy));
            for(int i=0;i<count;i++) {copy[i]=vertices[i];copy[i].x+=pass<2?1:0;copy[i].y+=pass>0?1:0;}
            sceGuDrawArray(w->dots?GU_POINTS:GU_LINE_STRIP,MD_FORMAT,count,NULL,copy);
        }
        sceGuDisable(GU_BLEND);
    }
    if(waveform) {
        const MdDecor *d=&frame_decor;
        ring=sceGuGetMemory(MD_WAVE_MAX_VERTICES*sizeof(*ring));
        float alpha=d->wave_alpha;
        if(spiral) alpha*=1.25f;
        if(mode==2 || mode==5) alpha*=.09f;
        if(mode==3) { float treb=md_signal_state.signal.values[9]; alpha=.15f*1.3f*treb*treb; }
        if(d->wave_mod_alpha) {
            float relative=(md_signal_state.signal.values[7]+md_signal_state.signal.values[8]+
                            md_signal_state.signal.values[9])/3;
            float mix=(relative-d->wave_mod_start)/(d->wave_mod_end-d->wave_mod_start);
            if(mix<0) mix=0;
            if(mix>1) mix=1;
            alpha*=mix;
        }
        if(alpha>1) alpha=1;
        float r=(custom_color&255)/255.0f,g=((custom_color>>8)&255)/255.0f,b=((custom_color>>16)&255)/255.0f;
        if(d->wave_brighten) {float peak=r>g?r:g; if(b>peak) peak=b; if(peak>0) {r/=peak;g/=peak;b/=peak;}}
        int wave_count=MD_WAVE_VERTICES,split=0;
        if(script) wave_count=md_wave_script(ring,md_right,md_left,md_custom_preset.wave_scale,
                                            md_custom_preset.wave_smoothing,md_rgba(r,g,b,alpha),d);
        else if(spiral) wave_count=md_wave_spiral(ring,md_right,md_left,md_custom_preset.wave_scale,
                         md_custom_preset.wave_smoothing,seconds,(float)height/width,md_rgba(r,g,b,alpha),d);
        else if(mode) wave_count=md_wave_extra(ring,mode,md_right,md_left,md_spectrum,md_custom_preset.wave_scale,
                          md_custom_preset.wave_smoothing,seconds,(float)height/width,md_rgba(r,g,b,alpha),d,&split);
        else md_wave_circle_style(ring,md_right,md_custom_preset.wave_scale,md_custom_preset.wave_smoothing,
                       seconds,(float)height/width,md_rgba(r,g,b,alpha),d);
        md_expand(ring, wave_count, 0);
        md_blend(d->wave_additive!=0);
        int primitive=d->wave_dots?GU_POINTS:GU_LINE_STRIP;
        sceGuDrawArray(primitive,MD_FORMAT,split?split:wave_count,NULL,ring);
        if(split) sceGuDrawArray(primitive,MD_FORMAT,wave_count-split,NULL,ring+split);
        if(d->wave_thick) for(int pass=0;pass<3;pass++) {
            MdVertex *copy=sceGuGetMemory(wave_count*sizeof(*copy));
            for(int i=0;i<wave_count;i++) {
                copy[i]=ring[i];
                copy[i].x+=pass<2?1:0; copy[i].y+=pass>0?1:0;
            }
            sceGuDrawArray(primitive,MD_FORMAT,split?split:wave_count,NULL,copy);
            if(split) sceGuDrawArray(primitive,MD_FORMAT,wave_count-split,NULL,copy+split);
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
        if(md_preset_state.effects[0]) md_darken_center((float)height/width);
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
    if(preset==3) md_image_effects(md_preset_state.effects);
    if(md_fade_image) {
        unsigned long long elapsed=now-md_fade_start;
        if(preset!=3 || elapsed>=(unsigned long long)md_fade_ms*1000) md_fade_clear();
        else {
            unsigned int alpha=255-(unsigned int)(elapsed*255/(md_fade_ms*1000ULL));
            sceGuEnable(GU_TEXTURE_2D);sceGuTexImage(0,MD_WIDTH,MD_HEIGHT,MD_WIDTH,md_fade_image);
            sceGuTexFlush();md_blend(0);
            for(int x=0;x<MD_WIDTH;x+=64) {
                MdVertex *v=sceGuGetMemory(2*sizeof(*v));
                v[0]=(MdVertex){(float)x,0,0xffffff|(alpha<<24),(float)x,0,0};
                v[1]=(MdVertex){(float)(x+64),MD_HEIGHT,0xffffff|(alpha<<24),(float)(x+64),MD_HEIGHT,0};
                sceGuDrawArray(GU_SPRITES,MD_FORMAT,2,NULL,v);
            }
            sceGuDisable(GU_BLEND);
        }
    }
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
