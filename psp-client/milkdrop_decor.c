/* SPDX-License-Identifier: GPL-2.0-or-later
 * New bounded geometry helpers following MilkDrop's fixed-function semantics. */
#include "milkdrop_decor.h"
#include <math.h>
static unsigned int channel(float x) { return (unsigned int)(fminf(1,fmaxf(0,x))*255); }
unsigned int md_rgba(float r,float g,float b,float a) {
    return channel(r)|(channel(g)<<8)|(channel(b)<<16)|(channel(a)<<24);
}
static unsigned int shape_channel(float value) {
    /* Desktop shapes use int(value*255) & 255, not normalized saturation.
     * Reduce before integer conversion to avoid overflow on extreme inputs.
     * Preserve the Desktop's single-precision product when it is finite. */
    if(!isfinite(value))return 0;
    float product=value*255.0f;
    if(product>=-2147483648.0f && product<2147483648.0f)
        return (unsigned int)(int)product&255U;
    double byte=fmod(trunc(isfinite(product)?(double)product:(double)value*255.0),256.0);
    if(byte<0)byte+=256;
    return (unsigned int)byte;
}
unsigned int md_shape_rgba(float r,float g,float b,float a) {
    return shape_channel(r)|(shape_channel(g)<<8)|(shape_channel(b)<<16)|(shape_channel(a)<<24);
}
float md_wave_opacity(const MdDecor *d,int mode,float bass,float mid,float treble) {
    /* MilkDrop 2 milkdropfs.cpp DrawWave: mode gain, then the unbounded
     * volume factor, then final alpha saturation. 512-wide feedback factors.
     * Mode 3 intentionally derives its alpha from treble instead of wave_a. */
    double alpha=d->wave_alpha;
    if(mode==1)alpha*=1.25;
    if(mode==2 || mode==5)alpha*=.09;
    if(mode==3)alpha=.15*1.3*treble*treble;
    if(d->wave_mod_alpha) {
        double span=(double)d->wave_mod_end-d->wave_mod_start;
        if(span<=0)return 0; /* loader/evaluator also reject this interval */
        alpha*=(((double)bass+mid+treble)*.333-d->wave_mod_start)/span;
    }
    if(alpha<=0)return 0;
    if(alpha>=1)return 1;
    return isfinite(alpha)?(float)alpha:0;
}
int md_shape_sides(float value) {
    if(!isfinite(value))return 0;
    /* Clamp before conversion so extreme finite formula results cannot
     * overflow an int. Desktop truncates then clamps to the same 3..100. */
    if(value<3)return 3;
    if(value>MD_SHAPE_SIDES)return MD_SHAPE_SIDES;
    return (int)value;
}
int md_shape_vertices(MdVertex *v,const MdShape *p,float aspect) {
    int sides=md_shape_sides(p->sides);
    if(!sides || p->tex_zoom<=0) return 0;
    unsigned int edge=md_shape_rgba(p->r2,p->g2,p->b2,p->a2);
    v[0]=(MdVertex){128,128,md_shape_rgba(p->r,p->g,p->b,p->a),p->x*256,p->y*256,0};
    for(int i=0;i<sides;i++) {
        float angle=i*(6.283185307f/sides)+.785398163f;
        v[i+1]=(MdVertex){128+128*cosf(angle+p->tex_ang)*aspect/p->tex_zoom,
            128+128*sinf(angle+p->tex_ang)/p->tex_zoom,edge,
            p->x*256+128*p->rad*cosf(angle+p->ang)*aspect,
            p->y*256-128*p->rad*sinf(angle+p->ang),0};
    }
    v[sides+1]=v[1]; return sides+2;
}
void md_echo_uv(float x,float y,float zoom,int orientation,float *u,float *v) {
    float a=.5f+(x-.5f)/zoom,b=.5f+(y-.5f)/zoom;
    *u=((orientation&1)?1-a:a)*256;
    *v=((orientation&2)?1-b:b)*256;
}
/* Reverse sample the same two triangles used by the warp, including its
 * per-grid equations. Positions and UVs here are physical feedback texels. */
int md_motion_vertices(MdVertex *out,const MdVertex *mesh,const float p[9]) {
    /* MilkDrop truncates fractional density, limiting the grid to 64x48.
     * Clamp before conversion so huge finite values cannot overflow int. */
    float density_x=p[4]>=65?64:p[4],density_y=p[5]>=49?48:p[5];
    if(!isfinite(density_x)||!isfinite(density_y)||density_x<1||density_y<1||p[0]<=0)return 0;
    int nx=(int)density_x,ny=(int)density_y,count=0;
    unsigned int color=md_rgba(p[1],p[2],p[3],p[0]);
    for(int y=0;y<ny;y++) for(int x=0;x<nx;x++) {
        float px=(x+.25f)/(density_x-.75f)+p[6],py=(y+.25f)/(density_y-.75f)-p[7];
        if(px<=.0001f || px>=.9999f || py<=.0001f || py>=.9999f) continue;
        int gx=(int)(px*8),gy=(int)(py*8);
        float fx=px*8-gx,fy=py*8-gy,w[3];
        const MdVertex *tri=mesh+(gy*8+gx)*6;
        if(fx+fy<=1) { w[0]=1-fx-fy; w[1]=fx; w[2]=fy; }
        else { tri+=3; w[0]=1-fy; w[1]=fx+fy-1; w[2]=1-fx; }
        float u=0,v=0;
        for(int i=0;i<3;i++) { u+=tri[i].u*w[i]; v+=tri[i].v*w[i]; }
        float dx=(u-.5f-px*512)*p[8],dy=(v-.5f-py*256)*p[8];
        float length=sqrtf(dx*dx+dy*dy);
        if(length<1) { if(length>1e-8f) { dx/=length; dy/=length; } else { dx=1; dy=1; } }
        /* Clip the segment, not each coordinate independently, before GU's
         * finite 2D coordinate range can wrap a long motion vector. */
        float t=1;
        if(dx>0) t=fminf(t,(512-px*512)/dx);
        if(dx<0) t=fminf(t,-px*512/dx);
        if(dy>0) t=fminf(t,(256-py*256)/dy);
        if(dy<0) t=fminf(t,-py*256/dy);
        dx*=t; dy*=t;
        out[count++]=(MdVertex){0,0,color,px*512,py*256,0};
        out[count++]=(MdVertex){0,0,color,px*512+dx,py*256+dy,0};
    }
    return count;
}
