/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PSPSTREAMER_MILKDROP_DECOR_H
#define PSPSTREAMER_MILKDROP_DECOR_H
#include "milkdrop_warp.h"
enum { MD_SHAPES=4, MD_SHAPE_SIDES=32, MD_DECOR_VALUES=25 };
typedef struct {
    float enabled,sides,additive,textured;
    float x,y,rad,ang,tex_ang,tex_zoom;
    float r,g,b,a,r2,g2,b2,a2,border_r,border_g,border_b,border_a;
} MdShape;
typedef struct { float size,r,g,b,a; } MdBorder;
typedef struct {
    float wave_x,wave_y,wave_param,wave_dots,wave_thick,wave_additive,wave_brighten;
    float wave_mod_alpha,wave_mod_start,wave_mod_end;
    float echo_zoom,echo_alpha,echo_orient;
    MdBorder outer,inner;
    float gamma,wave_alpha;
    MdShape shapes[MD_SHAPES];
} MdDecor;
unsigned int md_rgba(float r,float g,float b,float a);
int md_shape_vertices(MdVertex *out,const MdShape *shape,float aspect);
void md_echo_uv(float x,float y,float zoom,int orientation,float *u,float *v);
#endif
