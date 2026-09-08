#ifndef PSPSTREAMER_MILKDROP_WARP_H
#define PSPSTREAMER_MILKDROP_WARP_H
#include <stdint.h>

/* GU-compatible textured/color vertex; also exercised by the host tests. */
typedef struct { float u, v; uint32_t color; float x, y, z; } MdVertex;
enum { MD_GRID = 8, MD_TEXTURE = 256, MD_MESH_VERTICES = MD_GRID * MD_GRID * 6 };
typedef struct {
    float zoom, rotation, warp, warp_speed, warp_scale, decay;
} MdPreset;
extern const MdPreset md_presets[3];
void md_warp_mesh(MdVertex *vertices, const MdPreset *preset, float seconds);
void md_audio_ring(MdVertex *vertices, const unsigned char bands[12],
                   int level, float seconds, int variant);

/* PSP-only adapter: owns GU lists/textures solely during music visualization. */
int md_start(void);
void md_stop(void);
int md_frame(int tv, int fullscreen, const unsigned char bands[12], int level,
              unsigned long long now, int preset);
#endif
