#ifndef PSPSTREAMER_MILKDROP_WARP_H
#define PSPSTREAMER_MILKDROP_WARP_H
#include <stdint.h>

/* GU-compatible textured/color vertex; also exercised by the host tests. */
typedef struct { float u, v; uint32_t color; float x, y, z; } MdVertex;
enum { MD_GRID = 16, MD_TEXTURE = 256, MD_MESH_VERTICES = MD_GRID * MD_GRID * 6 };
enum { MD_GRID_POINTS = (MD_GRID+1)*(MD_GRID+1) };
typedef struct {
    float zoom, rotation, warp, warp_speed, warp_scale, decay;
    float dx, dy;
    float cx,cy,sx,sy,zoomexp;
} MdPreset;
extern const MdPreset md_presets[3];
void md_warp_mesh(MdVertex *vertices, const MdPreset *preset, float seconds);
void md_warp_mesh_varying(MdVertex *vertices, const MdPreset *preset,
                          const MdPreset *points, float seconds);
void md_audio_ring(MdVertex *vertices, const unsigned char bands[12],
                   int level, float seconds, int variant);

/* PSP-only adapter: owns GU lists/textures solely during music visualization. */
int md_start(void);
/* Applied by md_frame at a synchronized buffer rebuild; no app restart. */
extern int md_high_resolution;
/* Snapshot the previous presentation, restart custom state, fade into the new preset. */
void md_begin_preset(unsigned int fade_ms);
void md_stop(void);
void md_set_tv_title_bottom(int bottom);
int md_cave_control(int toggle,int analog_x,int analog_y,int throttle,int roll);
void md_profile_reset(int enabled);
void md_profile_select(const char *name,int tv,int fullscreen,int preset);
int md_profile_report(int index,char *text,int size);
/* Renderer-only diagnostic callback. persist is limited to initial frames. */
extern void (*md_trace_hook)(const char *stage,int persist);
/* 1: rendered/throttled, 0: GU failure, -1: invalid custom formula (see md_runtime_error). */
int md_frame(int tv, int fullscreen, const unsigned char bands[12], int level,
              unsigned long long now, int preset);
#endif
