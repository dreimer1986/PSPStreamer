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
 * Fixed zoom exponent=1, centre=(.5,.5), stretch=1; variable translation,
 * square feedback texture. No EEL, custom waves/shapes or shader interpreter.
 * Audio ring below is new PSPStreamer code, NOT MilkDrop's waveform engine. */
#include "milkdrop_warp.h"
#include <math.h>

const MdPreset md_presets[3] = {
    {1.025f,  0.015f, 0.8f, 1.0f, 1.0f, 0.97f, 0, 0},
    {1.008f, -0.030f, 1.8f, 0.7f, 1.5f, 0.96f, 0, 0},
    {1.045f,  0.004f, 0.4f, 1.4f, 0.8f, 0.98f, 0, 0}
};

void md_warp_mesh(MdVertex *vertices, const MdPreset *p, float seconds) {
    MdVertex grid[(MD_GRID + 1) * (MD_GRID + 1)];
    float time = seconds * p->warp_speed, inv_scale = 1.0f / p->warp_scale;
    float f[4] = {11.68f + 4*cosf(time*1.413f + 10),
                   8.77f + 3*cosf(time*1.113f + 7),
                  10.54f + 3*cosf(time*1.233f + 3),
                  11.49f + 4*cosf(time*0.933f + 5)};
    float c = cosf(p->rotation), s = sinf(p->rotation);
    unsigned int decay = (unsigned int)(p->decay * 255);
    int x, y, n = 0;
    for (y = 0; y <= MD_GRID; y++) for (x = 0; x <= MD_GRID; x++) {
        float px = 2.0f*x/MD_GRID - 1, py = 1 - 2.0f*y/MD_GRID;
        float u = px*.5f/p->zoom + .5f, v = -py*.5f/p->zoom + .5f;
        float a, b;
        MdVertex *out = &grid[y*(MD_GRID+1)+x];
        u += p->warp*.0035f*sinf(time*.333f + inv_scale*(px*f[0]-py*f[3]));
        v += p->warp*.0035f*cosf(time*.375f - inv_scale*(px*f[2]+py*f[1]));
        u += p->warp*.0035f*cosf(time*.753f - inv_scale*(px*f[1]-py*f[2]));
        v += p->warp*.0035f*sinf(time*.825f + inv_scale*(px*f[0]+py*f[3]));
        a = u-.5f; b = v-.5f;
        out->u = (a*c-b*s+.5f-p->dx)*MD_TEXTURE + .5f;
        out->v = (a*s+b*c+.5f-p->dy)*MD_TEXTURE + .5f;
        out->x = (float)x*MD_TEXTURE/MD_GRID;
        out->y = (float)y*MD_TEXTURE/MD_GRID; out->z = 0;
        out->color = 0xff000000U | decay | (decay<<8) | (decay<<16);
    }
    for (y = 0; y < MD_GRID; y++) for (x = 0; x < MD_GRID; x++) {
        int a = y*(MD_GRID+1)+x, b = a+1, c0 = a+MD_GRID+1, d = c0+1;
        vertices[n++] = grid[a]; vertices[n++] = grid[b]; vertices[n++] = grid[c0];
        vertices[n++] = grid[b]; vertices[n++] = grid[d]; vertices[n++] = grid[c0];
    }
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
