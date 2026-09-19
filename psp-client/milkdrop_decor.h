/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PSPSTREAMER_MILKDROP_DECOR_H
#define PSPSTREAMER_MILKDROP_DECOR_H
#include "milkdrop_warp.h"
enum { MD_SHAPES=4, MD_SHAPE_INSTANCES=8, MD_RENDER_SHAPES=32, MD_SHAPE_SIDES=100, MD_DECOR_VALUES=25 };
enum { MD_SHAPE_MAX_INSTANCES=512, MD_SHAPE_BATCH=32 };
typedef struct {
    float enabled,sides,additive,textured;
    float x,y,rad,ang,tex_ang,tex_zoom;
    float r,g,b,a,r2,g2,b2,a2,border_r,border_g,border_b,border_a;
    float thick_outline;
} MdShape;
/* Compact evaluated parameters, not expanded GU geometry. Renderer scratch;
 * contents are consumable only after successful frame evaluation. */
typedef struct { MdShape shapes[MD_SHAPES][MD_SHAPE_MAX_INSTANCES]; int count[MD_SHAPES]; } MdShapeFrame;
typedef struct { float size,r,g,b,a; } MdBorder;
typedef struct {
    float wave_x,wave_y,wave_param,wave_dots,wave_thick,wave_additive,wave_brighten;
    float wave_mod_alpha,wave_mod_start,wave_mod_end;
    float echo_zoom,echo_alpha,echo_orient;
    MdBorder outer,inner;
    float gamma,wave_alpha;
    MdShape shapes[MD_RENDER_SHAPES];
} MdDecor;
unsigned int md_rgba(float r,float g,float b,float a);
unsigned int md_shape_rgba(float r,float g,float b,float a);
void md_shader_colors(float colors[4][3],float seconds,float amount,const float phase[4]);
#define MD_MOTION_MAX_VERTICES (64*48*2)
float md_wave_opacity(const MdDecor *decor,int mode,float bass,float mid,float treble);
int md_shape_vertices(MdVertex *out,const MdShape *shape,float aspect);
int md_shape_sides(float value);
void md_echo_uv(float x,float y,float zoom,int orientation,float *u,float *v);
int md_motion_vertices(MdVertex *out,const MdVertex *expanded_mesh,const float settings[9]);
#endif
