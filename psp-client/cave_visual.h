/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PSPSTREAMER_CAVE_VISUAL_H
#define PSPSTREAMER_CAVE_VISUAL_H
#include "tunnel_visual.h"
enum { CAVE_GRID=12,CAVE_SLICES=16,CAVE_MAX_VERTICES=CAVE_GRID*CAVE_GRID*30 };
typedef struct {
    MdVertex vertices[CAVE_MAX_VERTICES];
    int index,count,padding[14]; /* Keep every DMA slice aligned to 64 bytes. */
} CaveSlice __attribute__((aligned(64)));
typedef struct {
    CaveSlice slices[CAVE_SLICES];
    TunnelState motion;
    int next,ready,built;
    float noise[16*16*16];
} CaveScene;
CaveScene *cave_create(void);
void cave_destroy(CaveScene *scene);
void cave_camera(float z,float *x,float *y);
/* Builds no more than one new slab per tick. Returned slab needs DMA writeback. */
CaveSlice *cave_prepare(CaveScene *scene,const unsigned char bands[12],int level,unsigned long long now);
float cave_density(const CaveScene *scene,float x,float y,float z);
/* Independent cube polygonizer, exposed for exhaustive topology bounds checks. */
int cave_polygonize(const float values[8],MdVertex *out,int capacity,float x,float y,float z);
#endif
