/* SPDX-License-Identifier: GPL-2.0-or-later
 * New bounded geometry helpers following MilkDrop's fixed-function semantics. */
#include "milkdrop_decor.h"
#include <math.h>
static unsigned int channel(float x) { return (unsigned int)(fminf(1,fmaxf(0,x))*255); }
unsigned int md_rgba(float r,float g,float b,float a) {
    return channel(r)|(channel(g)<<8)|(channel(b)<<16)|(channel(a)<<24);
}
int md_shape_vertices(MdVertex *v,const MdShape *p,float aspect) {
    int sides=(int)p->sides;
    if(sides<3 || sides>MD_SHAPE_SIDES || p->tex_zoom<=0) return 0;
    unsigned int edge=md_rgba(p->r2,p->g2,p->b2,p->a2);
    v[0]=(MdVertex){128,128,md_rgba(p->r,p->g,p->b,p->a),p->x*256,p->y*256,0};
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
