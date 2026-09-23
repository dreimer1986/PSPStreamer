/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PSPSTREAMER_CAVE_VISUAL_H
#define PSPSTREAMER_CAVE_VISUAL_H
#include "milkdrop_warp.h"
#include "cave_paths.h"
enum { CAVE_GRID=12,CAVE_AHEAD=16,CAVE_HISTORY=3,CAVE_SLICES=CAVE_AHEAD+CAVE_HISTORY,
       CAVE_TEXTURE=64,CAVE_MAX_VERTICES=CAVE_GRID*CAVE_GRID*30 };
_Static_assert(CAVE_PATH_CACHE>=CAVE_SLICES+3,"Path cache must retain camera and both future field profiles");
typedef struct {float travel,bass,phase,pulse;unsigned long long previous;} CaveMotion;
typedef struct {
    float field[CAVE_GRID+1][CAVE_GRID+1];
    float gradient[CAVE_GRID+1][CAVE_GRID+1][3];
    int z;
} CavePlane;
typedef struct {
    MdVertex vertices[CAVE_MAX_VERTICES];
    int index,count,padding[14]; /* Keep every DMA slice aligned to 64 bytes. */
} CaveSlice __attribute__((aligned(64)));
typedef struct {
    CaveSlice slices[CAVE_SLICES];
    CaveMotion motion;
    int next,ready,built;
    float noise[16*16*16];
    float noise_matrix[3][9],noise_offset[3][3];
    CavePlane planes[2];
    int sampled_planes;
    CavePaths paths;
} CaveScene;
void cave_texture(uint32_t *pixels);
void cave_noise_rotation(float out[9],float a,float b);
CaveScene *cave_create(void);
void cave_destroy(CaveScene *scene);
void cave_camera(const CaveScene *scene,float z,float *x,float *y);
/* Column-major OpenGL/GE view: six-profile look-ahead, bounded roll/sway. */
void cave_view(const CaveScene *scene,float z,float matrix[16]);
/* Builds no more than one new slab per tick. Returned slab needs DMA writeback. */
CaveSlice *cave_prepare(CaveScene *scene,const unsigned char bands[12],int level,unsigned long long now);
float cave_density(const CaveScene *scene,float x,float y,float z);
float cave_sample(const CaveScene *scene,float x,float y,float z,float gradient[3]);
/* Independent cube polygonizer, exposed for exhaustive topology bounds checks. */
int cave_polygonize(const float values[8],MdVertex *out,int capacity,float x,float y,float z);
#endif
