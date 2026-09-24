/*
  LICENSE
  -------
Copyright 2005-2013 Nullsoft, Inc.
All rights reserved.

Redistribution and use in source and binary forms, with or without modification,
are permitted provided that the following conditions are met:

  * Redistributions of source code must retain the above copyright notice,
    this list of conditions and the following disclaimer.

  * Redistributions in binary form must reproduce the above copyright notice,
    this list of conditions and the following disclaimer in the documentation
    and/or other materials provided with the distribution.

  * Neither the name of Nullsoft nor the names of its contributors may be used to
    endorse or promote products derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR
IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR
CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/


/* Modified PSP adaptation of the no-shader UV equations in MilkDrop 2's
 * WarpedBlit_NoShaders (milkdropfs.cpp), revision
 * b5e4136c2f050eafa10aa199bb72c8e5c12c9320.
 * Bounded zoom exponent, centre, stretch and translation;
 * square feedback texture. No per-vertex EEL or shader interpreter here.
 * Audio ring below is new PSPStreamer code, NOT MilkDrop's waveform engine. */
#include "milkdrop_warp.h"
#include <math.h>
#include <stddef.h>

const MdPreset md_presets[3] = {
    {1.025f,  0.015f, 0.8f, 1.0f, 1.0f, 0.97f, 0, 0, .5f,.5f,1,1,1},
    {1.008f, -0.030f, 1.8f, 0.7f, 1.5f, 0.96f, 0, 0, .5f,.5f,1,1,1},
    {1.045f,  0.004f, 0.4f, 1.4f, 0.8f, 0.98f, 0, 0, .5f,.5f,1,1,1}
};

void md_warp_mesh(MdVertex *vertices, const MdPreset *p, float seconds) {
    md_warp_mesh_varying(vertices,p,NULL,seconds);
}
static void md_warp_grid(MdVertex grid[MD_GRID_POINTS], const MdPreset *p, const MdPreset *points, float seconds) {
    float time = seconds * p->warp_speed, inv_scale = 1.0f / p->warp_scale;
    float f[4] = {11.68f + 4*cosf(time*1.413f + 10),
                   8.77f + 3*cosf(time*1.113f + 7),
                  10.54f + 3*cosf(time*1.233f + 3),
                  11.49f + 4*cosf(time*0.933f + 5)};
    float c = cosf(p->rotation), s = sinf(p->rotation);
    float rotation=p->rotation;
    static float radii[MD_GRID_POINTS];
    static int radii_ready;
    if(!radii_ready) {
        for(int y=0;y<=MD_GRID;y++) for(int x=0;x<=MD_GRID;x++) {
            float px=2.0f*x/MD_GRID-1,py=1-2.0f*y/MD_GRID;
            radii[y*(MD_GRID+1)+x]=sqrtf(px*px+py*py);
        }
        radii_ready=1;
    }
    unsigned int decay = (unsigned int)(p->decay * 255);
    int x, y;
    for (y = 0; y <= MD_GRID; y++) for (x = 0; x <= MD_GRID; x++) {
        if(points) {
            p=&points[y*(MD_GRID+1)+x];
            if(p->rotation!=rotation) {
                rotation=p->rotation;c=cosf(rotation);s=sinf(rotation);
            }
        }
        float px = 2.0f*x/MD_GRID - 1, py = 1 - 2.0f*y/MD_GRID;
        float zoom=p->zoom;
        /* 1 raised to any exponent remains 1, including overflow of the
         * inner power. Avoid two libm calls per vertex without approximating
         * the non-unit zoom path or changing per-pixel formula execution. */
        if(zoom!=1 && p->zoomexp!=1) {
            float radius=radii[y*(MD_GRID+1)+x];
            zoom=powf(p->zoom,powf(p->zoomexp,radius*2-1));
        }
        /* Bound effective magnification before division, including power
         * under/overflow from otherwise finite preset values. */
        if(zoom<1.0f/65536)zoom=1.0f/65536;
        if(zoom>65536)zoom=65536;
        float u = px*.5f/zoom + .5f, v = -py*.5f/zoom + .5f;
        u=(u-p->cx)/p->sx+p->cx; v=(v-p->cy)/p->sy+p->cy;
        float a, b;
        MdVertex *out = &grid[y*(MD_GRID+1)+x];
        if(p->warp!=0) {
        u += p->warp*.0035f*sinf(time*.333f + inv_scale*(px*f[0]-py*f[3]));
        v += p->warp*.0035f*cosf(time*.375f - inv_scale*(px*f[2]+py*f[1]));
        u += p->warp*.0035f*cosf(time*.753f - inv_scale*(px*f[1]-py*f[2]));
        v += p->warp*.0035f*sinf(time*.825f + inv_scale*(px*f[0]+py*f[3]));
        }
        a = u-p->cx; b = v-p->cy;
        out->u = (a*c-b*s+p->cx-p->dx)*MD_TEXTURE + .5f;
        out->v = (a*s+b*c+p->cy-p->dy)*MD_TEXTURE + .5f;
        if(out->u>4096)out->u=4096;else if(out->u< -4096)out->u=-4096;
        if(out->v>4096)out->v=4096;else if(out->v< -4096)out->v=-4096;
        out->x = (float)x*MD_TEXTURE/MD_GRID;
        out->y = (float)y*MD_TEXTURE/MD_GRID; out->z = 0;
        out->color = 0xff000000U | decay | (decay<<8) | (decay<<16);
    }
}
static void md_expand_grid(MdVertex *vertices,const MdVertex grid[MD_GRID_POINTS]) {
    int n=0;
    for (int y = 0; y < MD_GRID; y++) for (int x = 0; x < MD_GRID; x++) {
        int a = y*(MD_GRID+1)+x, b = a+1, c0 = a+MD_GRID+1, d = c0+1;
        vertices[n++] = grid[a]; vertices[n++] = grid[b]; vertices[n++] = grid[c0];
        vertices[n++] = grid[b]; vertices[n++] = grid[d]; vertices[n++] = grid[c0];
    }
}
void md_warp_mesh_varying(MdVertex *vertices, const MdPreset *p, const MdPreset *points, float seconds) {
    MdVertex grid[MD_GRID_POINTS];
    md_warp_grid(grid,p,points,seconds);
    md_expand_grid(vertices,grid);
}
void md_warp_mesh_blended(MdVertex *vertices,const MdPreset *fresh,const MdPreset *fresh_points,float seconds,
                         const MdPreset *old,const MdPreset *old_points,float old_seconds,float weight) {
    /* Render-thread scratch: blend 289 unique vertices, not 1536 duplicated
     * triangle corners. Keep the second grid off the PSP stack and GU list. */
    static MdVertex previous[MD_GRID_POINTS];
    MdVertex grid[MD_GRID_POINTS];
    md_warp_grid(grid,fresh,fresh_points,seconds);
    md_warp_grid(previous,old,old_points,old_seconds);
    /* Warp decay is uniform even when UV coordinates have per-pixel formulas. */
    unsigned color=0;
    for(int channel=0;channel<4;channel++) {
        int a=(previous[0].color>>(8*channel))&255,b=(grid[0].color>>(8*channel))&255;
        color|=(unsigned)(a+(b-a)*weight)<<(8*channel);
    }
    for(int i=0;i<MD_GRID_POINTS;i++) {
        grid[i].u=previous[i].u+(grid[i].u-previous[i].u)*weight;
        grid[i].v=previous[i].v+(grid[i].v-previous[i].v)*weight;
        grid[i].color=color;
    }
    md_expand_grid(vertices,grid);
}

void md_audio_ring(MdVertex *vertices, const unsigned char bands[12],
                   int level, float seconds, int variant) {
    int i;
    static float circle_x[96], circle_y[96];
    static int circle_ready;
    unsigned int r = (unsigned int)(128+110*sinf(seconds*.4f+variant*2));
    unsigned int g = (unsigned int)(128+110*sinf(seconds*.4f+2+variant*2));
    unsigned int b = (unsigned int)(128+110*sinf(seconds*.4f+4+variant*2));
    if (!circle_ready) {
        for (i = 0; i < 96; i++) {
            float angle = (float)i*6.283185307f/96;
            circle_x[i] = cosf(angle); circle_y[i] = sinf(angle);
        }
        circle_ready = 1;
    }
    /* The producer already clamps these snapshots to 0..100. */
    if (level < 0) level = 0;
    if (level > 100) level = 100;
    for (i = 0; i <= 96; i++) {
        int band = (i % 96)/8;
        float radius = 35 + level*.18f + bands[band]*.38f;
        vertices[i].u = vertices[i].v = vertices[i].z = 0;
        vertices[i].x = 128 + radius*circle_x[i % 96];
        vertices[i].y = 128 + radius*circle_y[i % 96];
        vertices[i].color = 0xff000000U | r | (g<<8) | (b<<16);
    }
}
