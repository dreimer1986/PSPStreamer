/* SPDX-License-Identifier: GPL-2.0-or-later
 * PSP adapter, no display-mode changes, no audio/ME calls. */
#include "milkdrop_warp.h"
#include "milkdrop_preset.h"
#include "milkdrop_wave.h"
#include "milkdrop_texture.h"
#include <pspgu.h>
#include <pspge.h>
#include <pspkernel.h>
#include <malloc.h>
#include <string.h>
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include "milkdrop_profile.h"

/* Even native TV scanout ends before these textures. Real 2 MiB EDRAM only. */
#define MD_WIDTH 512
static int md_height=512;
int md_high_resolution=1;
static int md_last_high_resolution;
#define MD_HEIGHT md_height
/* TV scanout leaves room for one 512-square RGB565 surface plus scratch.
 * Preserve raw feedback in main RAM via GE copy, never a CPU frame copy. */
static void *md_raw_image;
static int md_texture_base, md_texture_bytes, md_pixel_format;
#define MD_TEXTURE_BYTES md_texture_bytes
#define MD_TEXTURE_BASE md_texture_base
/* Main RAM, not EDRAM: four 1024-point thick waves plus a 16x16 mesh. */
#define MD_LIST_BYTES 1572864
#define MD_FORMAT (GU_TEXTURE_32BITF | GU_COLOR_8888 | GU_VERTEX_32BITF | GU_TRANSFORM_2D)
typedef struct { unsigned int color; float x,y,z; } MdPlainVertex;
_Static_assert(sizeof(MdPlainVertex)==16,"untextured GU vertex layout");
/* Keep immutable storage until GU completion. No texture coordinates are
 * consumed by these untextured passes; prepare all three in one traversal. */
static void md_thick_wave(int primitive,const MdVertex *vertices,int count,int split) {
    /* Desktop offsets toward positive D3D y; PSP screen y points downward. */
    MdPlainVertex *copies=sceGuGetMemory(3*count*sizeof(*copies));
    for(int i=0;i<count;i++) {
        const MdVertex *v=&vertices[i];
        copies[i]=(MdPlainVertex){v->color,v->x+1,v->y,v->z};
        copies[count+i]=(MdPlainVertex){v->color,v->x+1,v->y-1,v->z};
        copies[2*count+i]=(MdPlainVertex){v->color,v->x,v->y-1,v->z};
    }
    int format=GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_2D;
    for(int pass=0;pass<3;pass++) {
        MdPlainVertex *v=copies+pass*count;
        sceGuDrawArray(primitive,format,split?split:count,NULL,v);
        if(split)sceGuDrawArray(primitive,format,count-split,NULL,v+split);
    }
}
static unsigned int *md_list;
static int md_front;
static int md_last_tv, md_last_fullscreen;
static int md_tv_top = 86, md_last_top;
static unsigned long long md_origin, md_next;
static MdSignalState md_signal_state;
static MdPresetState md_preset_state;
static MdShapeFrame *md_shape_frame;
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
static float md_shader_phase[4];
static unsigned long long md_shader_epoch;
static int md_shader_seeded;
static MdImage md_images[MD_SHAPES];
static int md_images_ready;
static void md_images_clear(void) {
    for(int i=0;i<MD_SHAPES;i++) md_image_free(&md_images[i]);
    md_images_ready=0;
}
static int md_images_prepare(void) {
    if(md_images_ready) return 1;
    for(int i=0;i<MD_SHAPES;i++) if(md_custom_preset.texture_path[i][0]) {
        if(!md_image_load(md_custom_preset.texture_path[i],&md_images[i])) {
            md_images_clear();
            md_runtime_error.code=MD_FILE_IO; md_runtime_error.line=0;
            snprintf(md_runtime_error.key,sizeof(md_runtime_error.key),"psp_texture_%d",i);
            return 0;
        }
        sceKernelDcacheWritebackRange(md_images[i].pixels,md_images[i].width*md_images[i].height*4);
    }
    md_images_ready=1;
    return 1;
}
static void md_fade_clear(void) {free(md_fade_image);md_fade_image=NULL;md_fade_ms=0;}
void md_begin_preset(unsigned int fade_ms) {
    if(md_list) sceGuSync(GU_SYNC_FINISH,GU_SYNC_WHAT_DONE);
    md_images_clear();
    md_fade_clear();
    if(md_list && md_origin && fade_ms) {
        sceGuSync(GU_SYNC_FINISH,GU_SYNC_WHAT_DONE);
        md_fade_image=memalign(64,MD_TEXTURE_BYTES);
        if(md_fade_image) {
            memcpy(md_fade_image,(void *)(uintptr_t)(0x44000000+MD_TEXTURE_BASE+(md_raw_image?0:1-md_front)*MD_TEXTURE_BYTES),MD_TEXTURE_BYTES);
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
        float scale=(float)MD_HEIGHT/MD_TEXTURE;
        v[i].y *= scale;
        v[i].v = half_texel ? (v[i].v-.5f)*scale+.5f : v[i].v*scale;
    }
}
void md_set_tv_title_bottom(int bottom) {
    int top = bottom + 5;
    md_tv_top = top < 70 ? 70 : top > 102 ? 102 : top;
}

static int md_offset(int index) {
    return MD_TEXTURE_BASE+(md_raw_image?(index==2?1:0):index)*MD_TEXTURE_BYTES;
}
static void *md_texture(int index) {
    return (void *)(uintptr_t)(0x04000000 + md_offset(index));
}
static void md_target(int offset, int stride, int width, int height) {
    /* Shape centers and edges have independent color/alpha. Flat shading
     * takes the last (often transparent) edge vertex for the whole triangle.
     * Restore explicitly, including after GU list restarts and UI switches. */
    sceGuShadeModel(GU_SMOOTH);
    sceGuDrawBufferList(offset ? md_pixel_format : GU_PSM_8888, (void *)(uintptr_t)offset, stride);
    sceGuOffset(2048-width/2, 2048-height/2);
    sceGuViewport(2048, 2048, width, height);
    sceGuScissor(0, 0, width, height);
}
#include "milkdrop_decor_gu.h"
/* Copy before list reuse; renderer-owned scratch avoids stack growth. */
static MdVertex md_clip_wave_source[2*MD_CUSTOM_POINTS-1];
static int md_draw_wave(int primitive,const MdVertex *v,int count,int split,int thick) {
    if(count<=0)return 1;
    if(count>2*MD_CUSTOM_POINTS-1)return 0;
    int clipped=0;
    for(int i=0;i<count;i++)
        if(v[i].x<0 || v[i].x>MD_WIDTH-(thick?1:0) ||
           v[i].y<0 || v[i].y>MD_HEIGHT-(thick?1:0))clipped=1;
    if(!clipped) {
        sceGuDrawArray(primitive,MD_FORMAT,split?split:count,NULL,v);
        if(split)sceGuDrawArray(primitive,MD_FORMAT,count-split,NULL,v+split);
        if(thick)md_thick_wave(primitive,v,count,split);
        return 1;
    }
    memcpy(md_clip_wave_source,v,count*sizeof(*v));
    sceGuFinish();sceGuSync(GU_SYNC_FINISH,GU_SYNC_WHAT_DONE);
    if(sceGuStart(GU_DIRECT,md_list)<0)return 0;
    md_target(md_offset(1-md_front),MD_WIDTH,MD_WIDTH,MD_HEIGHT);
    static const float dx[]={0,1,1,0},dy[]={0,0,-1,-1};
    for(int pass=0;pass<(thick?4:1);pass++) {
        MdPlainVertex *out=sceGuGetMemory(2*count*sizeof(*out));int used=0;
        for(int i=0;i<count;i++) {
            MdVertex a=md_clip_wave_source[i];a.x+=dx[pass];a.y+=dy[pass];
            if(primitive==GU_POINTS) {
                if(a.x>=0 && a.x<=MD_WIDTH && a.y>=0 && a.y<=MD_HEIGHT)
                    out[used++]=(MdPlainVertex){a.color,a.x,a.y,0};
            } else if(i+1<count && i+1!=split) {
                MdVertex b=md_clip_wave_source[i+1];b.x+=dx[pass];b.y+=dy[pass];
                MdVertex line[2]={a,b},cut[2];
                int n=md_clip_segment(cut,line,MD_WIDTH,MD_HEIGHT);
                for(int j=0;j<n;j++)out[used++]=(MdPlainVertex){cut[j].color,cut[j].x,cut[j].y,0};
            }
        }
        if(used)sceGuDrawArray(primitive==GU_POINTS?GU_POINTS:GU_LINES,
            GU_COLOR_8888|GU_VERTEX_32BITF|GU_TRANSFORM_2D,used,NULL,out);
    }
    return 1;
}
int md_start(void) {
    if (md_list) return 1;
    if (sceGeEdramGetSize() < 2*1024*1024) return 0;
    md_list = memalign(64, MD_LIST_BYTES);
    if (!md_list) return 0;
    md_shape_frame=malloc(sizeof(*md_shape_frame));
    if(!md_shape_frame){free(md_list);md_list=NULL;return 0;}
    if (sceGuInit() < 0) {
        free(md_shape_frame);md_shape_frame=NULL;
        free(md_list); md_list = NULL;
        return 0;
    }
    if(!md_shader_seeded) {
        static const unsigned int ranges[4]={64841,53751,42661,31571};
        md_shader_epoch=sceKernelGetSystemTimeWide();
        unsigned int seed=(unsigned int)md_shader_epoch^0x9e3779b9U;
        if(!seed)seed=1;
        for(int i=0;i<4;i++) {
            seed^=seed<<13;seed^=seed>>17;seed^=seed<<5;
            md_shader_phase[i]=(seed%ranges[i])*.01f;
        }
        md_shader_seeded=1;
    }
    md_front = 0; md_origin = md_next = 0;
    md_last_tv = md_last_fullscreen = -1;
    md_last_high_resolution=-1;
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
    md_images_clear();
    md_fade_clear();
    sceGuTerm();
    free(md_raw_image);md_raw_image=NULL;
    free(md_list); md_list = NULL;
    free(md_shape_frame);md_shape_frame=NULL;
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
    /* Once per activation, before starting any GU list. Never decode in the
     * shape loop. Failed assets use the existing preset-error UI. */
    if(preset==3 && !md_images_prepare()) return -1;
    if (now < md_next && tv == md_last_tv && md_high_resolution==md_last_high_resolution && fullscreen == md_last_fullscreen &&
        top == md_last_top) {
        if(md_profile_current>=0)md_profiles[md_profile_current].skipped++;
        return 1;
    }
    md_profile_begin();
    if (tv != md_last_tv || md_high_resolution!=md_last_high_resolution) {
        md_fade_clear();
        free(md_raw_image);md_raw_image=NULL;
        md_height=md_high_resolution?512:256;
        if(tv && md_high_resolution) {
            md_raw_image=memalign(64,MD_WIDTH*MD_HEIGHT*2);
            if(!md_raw_image)md_height=256; /* Proven two-surface fallback. */
            else {
                memset(md_raw_image,0,MD_WIDTH*MD_HEIGHT*2);
                sceKernelDcacheWritebackRange(md_raw_image,MD_WIDTH*MD_HEIGHT*2);
            }
        }
        md_texture_base = tv ? 768*480*4 : 512*272*4;
        md_pixel_format = (!tv && !md_high_resolution)?GU_PSM_8888:GU_PSM_5650;
        md_texture_bytes = MD_WIDTH*MD_HEIGHT*(md_pixel_format==GU_PSM_8888?4:2);
        int surfaces=md_raw_image?1:2;
        if ((unsigned int)(MD_TEXTURE_BASE + surfaces*MD_TEXTURE_BYTES + MD_TEXTURE_BYTES/8) > sceGeEdramGetSize()) return 0;
        memset((void *)(uintptr_t)(0x44000000 + MD_TEXTURE_BASE), 0, surfaces*MD_TEXTURE_BYTES);
        md_front=0; target=1;
    }
    if (!md_origin) md_origin = now;
    seconds = (float)(now-md_origin)/1000000;
    /* Render-thread scratch: extended EEL memories must not consume the PSP
     * stack twice before entering the frame/point evaluators. */
    static MdPresetState next_state;
    md_output_width=width;md_output_height=height;
    md_profile_mark(0);
    if (preset == 3) {
        if (!md_signal_active) {
            md_signal_reset(&md_signal_state);
            memset(&md_preset_state,0,sizeof(md_preset_state));
        }
        md_signal_active = 1;
        md_signal_update(&md_signal_state, bands, level, now);
        next_state=md_preset_state;
        if (md_eval_preset_shapes(&md_custom_preset, seconds, &md_signal_state.signal,
                                  &next_state, &evaluated, &custom_color, &frame_decor, &md_runtime_error,md_shape_frame) != MD_FILE_OK)
            return -1; /* No GU list was started; caller retains music playback. */
        md_profile_mark(1);
        if(md_custom_preset.pixel_program.count &&
           md_eval_pixel_grid(&md_custom_preset,&evaluated,seconds,&md_signal_state.signal,
                             &next_state,md_pixel_points,&md_runtime_error)!=MD_FILE_OK) return -1;
    } else {md_signal_active = 0;md_profile_mark(1);}
    md_profile_mark(2);
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
    md_profile_mark(3);
    if(custom_waves && md_eval_custom_waves(&md_custom_preset,seconds,&md_signal_state.signal,
            md_right,md_left,md_bins_left,md_bins_right,&next_state,md_custom_geometry,&md_runtime_error)!=MD_FILE_OK) return -1;
    if(preset==3) md_preset_state=next_state;
    md_profile_mark(4);
    if (fullscreen) left = top = 0;
    if (sceGuStart(GU_DIRECT, md_list) < 0) { md_stop(); return 0; }
    sceGuDisable(GU_DEPTH_TEST); sceGuDisable(GU_CULL_FACE);
    sceGuDisable(GU_LIGHTING); sceGuDisable(GU_BLEND);
    sceGuDisable(GU_ALPHA_TEST); sceGuDisable(GU_STENCIL_TEST);
    sceGuEnable(GU_SCISSOR_TEST); sceGuEnable(GU_TEXTURE_2D);
    md_target(md_offset(target), MD_WIDTH, MD_WIDTH, MD_HEIGHT);
    sceGuTexMode(md_pixel_format, 0, 0, 0);
    sceGuTexImage(0, MD_WIDTH, MD_HEIGHT, MD_WIDTH, md_raw_image?md_raw_image:md_texture(md_front));
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
    for(int i=0;i<MD_MESH_VERTICES;i++) if(!isfinite(mesh[i].u)||!isfinite(mesh[i].v)) {
        md_runtime_error.code=MD_FILE_INVALID;md_runtime_error.line=0;
        snprintf(md_runtime_error.key,sizeof(md_runtime_error.key),"warp geometry");
        sceGuFinish();sceGuSync(GU_SYNC_FINISH,GU_SYNC_WHAT_DONE);return -1;
    }
    sceGuDrawArray(GU_TRIANGLES, MD_FORMAT, MD_MESH_VERTICES, NULL, mesh);
    sceGuDisable(GU_TEXTURE_2D);
    if(preset==3 && md_preset_state.motion[0]>0) {
        MdVertex *vectors=sceGuGetMemory(MD_MOTION_MAX_VERTICES*sizeof(*vectors));
        int count=md_motion_vertices(vectors,mesh,md_preset_state.motion);
        if(count) { md_blend(0); sceGuDisable(GU_TEXTURE_2D); sceGuDrawArray(GU_LINES,MD_FORMAT,count,NULL,vectors); }
        sceGuDisable(GU_BLEND);
    }
    if(preset==3 && !md_shapes(md_shape_frame,(float)height/width)) {md_stop();return 0;}
    if(custom_waves) for(int slot=0;slot<MD_CUSTOM_WAVES;slot++) {
        const MdCustomWave *w=&md_custom_preset.waves[slot];
        int count=md_custom_geometry[slot].count;
        if(!count) continue;
        MdVertex *vertices=sceGuGetMemory((w->dots?count:count*2-1)*sizeof(*vertices));
        if(w->dots) memcpy(vertices,md_custom_geometry[slot].vertices,count*sizeof(*vertices));
        else count=md_wave_smooth(vertices,md_custom_geometry[slot].vertices,count);
        md_expand(vertices,count,0); md_blend(w->additive!=0);
        sceGuDisable(GU_TEXTURE_2D);
        if(!md_draw_wave(w->dots?GU_POINTS:GU_LINE_STRIP,vertices,count,0,w->thick!=0)) {md_stop();return 0;}
        sceGuDisable(GU_BLEND);
    }
    if(waveform) {
        const MdDecor *d=&frame_decor;
        float wave_scale=fminf(100,fmaxf(-100,md_custom_preset.wave_scale));
        ring=sceGuGetMemory(MD_WAVE_MAX_VERTICES*sizeof(*ring));
        float alpha=md_wave_opacity(d,mode,md_signal_state.signal.values[7],
            md_signal_state.signal.values[8],md_signal_state.signal.values[9]);
        float r=(custom_color&255)/255.0f,g=((custom_color>>8)&255)/255.0f,b=((custom_color>>16)&255)/255.0f;
        if(d->wave_brighten) {float peak=r>g?r:g; if(b>peak) peak=b; if(peak>0) {r/=peak;g/=peak;b/=peak;}}
        int wave_count=MD_WAVE_VERTICES,split=0;
        if(script) wave_count=md_wave_script(ring,md_right,md_left,wave_scale,
                                            md_custom_preset.wave_smoothing,md_rgba(r,g,b,alpha),d);
        else if(spiral) wave_count=md_wave_spiral(ring,md_right,md_left,wave_scale,
                         md_custom_preset.wave_smoothing,seconds,(float)height/width,md_rgba(r,g,b,alpha),d);
        else if(mode) wave_count=md_wave_extra(ring,mode,md_right,md_left,md_spectrum,wave_scale,
                          md_custom_preset.wave_smoothing,seconds,(float)height/width,md_rgba(r,g,b,alpha),d,&split);
        else md_wave_circle_style(ring,md_right,wave_scale,md_custom_preset.wave_smoothing,
                       seconds,(float)height/width,md_rgba(r,g,b,alpha),d);
        md_expand(ring, wave_count, 0);
        /* Finite offscreen coordinates are clipped before GU conversion. */
        for(int i=0;i<wave_count;i++) if(!isfinite(ring[i].x)||!isfinite(ring[i].y)) {
            md_runtime_error.code=MD_FILE_INVALID;md_runtime_error.line=0;
            snprintf(md_runtime_error.key,sizeof(md_runtime_error.key),"wave geometry");
            sceGuFinish();sceGuSync(GU_SYNC_FINISH,GU_SYNC_WHAT_DONE);
            return -1;
        }
        md_blend(d->wave_additive!=0);
        int primitive=d->wave_dots?GU_POINTS:GU_LINE_STRIP;
        if(!md_draw_wave(primitive,ring,wave_count,split,d->wave_thick!=0)) {md_stop();return 0;}
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
    if(md_raw_image) {
        sceGuCopyImage(md_pixel_format,0,0,MD_WIDTH,MD_HEIGHT,MD_WIDTH,md_texture(target),
                       0,0,MD_WIDTH,md_raw_image);
        sceGuTexSync();
    }
    md_target(md_offset(md_front), MD_WIDTH, MD_WIDTH, MD_HEIGHT);
    sceGuEnable(GU_TEXTURE_2D);
    sceGuTexImage(0, MD_WIDTH, MD_HEIGHT, MD_WIDTH, md_raw_image?md_raw_image:md_texture(target));
    sceGuTexWrap(GU_CLAMP, GU_CLAMP);
    sceGuTexFlush();
    const MdDecor *d=&frame_decor;
    float shade[4][3];
    const float (*shading)[3]=NULL;
    if(preset==3 && md_custom_preset.shader_amount>.001f) {
        md_shader_colors(shade,(float)(now-md_shader_epoch)/1000000,
                         md_custom_preset.shader_amount,md_shader_phase);
        shading=shade;
    }
    md_present(0,0,MD_WIDTH,MD_HEIGHT,preset==3?d->gamma:1,
        preset==3?d->echo_zoom:1,preset==3?d->echo_alpha:0,preset==3?(int)d->echo_orient:0,shading);
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
    md_present(left,top,width,height,1,1,0,0,NULL);
    sceGuFinish();
    md_profile_mark(5);
    sceGuSync(GU_SYNC_FINISH, GU_SYNC_WHAT_DONE);
    md_profile_mark(6);
    md_profile_commit();
    md_front = target;
    md_last_tv = tv; md_last_fullscreen = fullscreen;
    md_last_high_resolution=md_high_resolution;
    md_last_top = top;
    finished = sceKernelGetSystemTimeWide();
    cost = finished - now;
    /* Never attempt catch-up frames. Expensive frames lower the visual rate,
     * rather than changing audio clocks or consuming a whole CPU core.
     * Retain at least 50 ms idle; expensive frames rest twice their cost. */
    md_next = finished + (cost > 25000ULL ? cost*2 : 50000ULL);
    return 1;
}
