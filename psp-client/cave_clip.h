/* SPDX-License-Identifier: GPL-2.0-or-later
 * Clip before the PSP GE's screen-range rejection. GU_CLIP_PLANES does not
 * guarantee survival of a triangle with an out-of-range projected vertex.
 * Independent Sutherland-Hodgman implementation, world-space attributes. */
#ifndef PSPSTREAMER_CAVE_CLIP_H
#define PSPSTREAMER_CAVE_CLIP_H
#include "milkdrop_warp.h"
#include <math.h>
enum {CAVE_CLIP_PLANES=5,CAVE_CLIP_VERTICES=18};
typedef struct {float plane[CAVE_CLIP_PLANES][4];} CaveClip;
static inline void cave_clip_init(CaveClip *c,const float view[16],float px,float py) {
    /* Slightly outside the viewport; scissoring finishes the boundary. */
    for(int k=0;k<4;k++) {
        float depth=-view[k*4+2];
        c->plane[0][k]=depth;
        c->plane[1][k]=depth*1.01f+px*view[k*4];
        c->plane[2][k]=depth*1.01f-px*view[k*4];
        c->plane[3][k]=depth*1.01f+py*view[k*4+1];
        c->plane[4][k]=depth*1.01f-py*view[k*4+1];
    }
    c->plane[0][3]-=.11f; /* Projection near=.1; avoid roundoff at GE near. */
}
static inline float cave_clip_distance(const float *p,const MdVertex *v) {
    return p[0]*v->x+p[1]*v->y+p[2]*v->z+p[3];
}
static inline unsigned cave_clip_mask(const CaveClip *c,const MdVertex *v) {
    unsigned mask=0;
    for(int i=0;i<CAVE_CLIP_PLANES;i++)if(cave_clip_distance(c->plane[i],v)<0)mask|=1U<<i;
    return mask;
}
static inline MdVertex cave_clip_mix(const MdVertex *a,const MdVertex *b,float t) {
    MdVertex v={a->u+(b->u-a->u)*t,a->v+(b->v-a->v)*t,0,
        a->x+(b->x-a->x)*t,a->y+(b->y-a->y)*t,a->z+(b->z-a->z)*t};
    for(int k=0;k<32;k+=8) {
        int x=(a->color>>k)&255,y=(b->color>>k)&255;
        unsigned value=(unsigned)(x+(y-x)*t+.5f);
        v.color|=(value>255?255:value)<<k;
    }
    return v;
}
static inline void cave_clip_snap(const float p[4],MdVertex *v) {
    /* Cancellation on nearly parallel edges can leave the float intersection
     * outside its plane. Project the small residual back, not the whole edge. */
    float norm=p[0]*p[0]+p[1]*p[1]+p[2]*p[2];
    if(norm>1e-12f) {
        float correction=(cave_clip_distance(p,v)-.00001f)/norm;
        v->x-=p[0]*correction;v->y-=p[1]*correction;v->z-=p[2]*correction;
    }
}
static inline int cave_clip_line(const CaveClip *c,MdVertex *a,MdVertex *b) {
    for(int p=0;p<CAVE_CLIP_PLANES;p++) {
        float da=cave_clip_distance(c->plane[p],a),db=cave_clip_distance(c->plane[p],b);
        if(da<0 && db<0)return 0;
        if((da<0)!=(db<0)) {
            MdVertex v=cave_clip_mix(a,b,da/(da-db));
            cave_clip_snap(c->plane[p],&v);
            if(da<0)*a=v;else *b=v;
        }
    }
    return 1;
}
/* A triangle intersected with five half-spaces has at most eight corners.
 * Caller supplies 18 vertices for its triangle fan; input stays untouched. */
static inline int cave_clip_triangle(const CaveClip *c,const MdVertex input[3],MdVertex output[CAVE_CLIP_VERTICES]) {
    MdVertex buffers[2][8];int current=0,count=3;
    for(int i=0;i<3;i++)buffers[0][i]=input[i];
    for(int plane=0;plane<CAVE_CLIP_PLANES;plane++) {
        MdVertex *in=buffers[current],*out=buffers[!current];int used=0;
        MdVertex prev=in[count-1];float dp=cave_clip_distance(c->plane[plane],&prev);
        for(int i=0;i<count;i++) {
            MdVertex next=in[i];float dn=cave_clip_distance(c->plane[plane],&next);
            if((dp<0)!=(dn<0)) {
                if(used>=8)return -1;
                float t=dp/(dp-dn);if(t<0)t=0;if(t>1)t=1;
                out[used]=cave_clip_mix(&prev,&next,t);
                cave_clip_snap(c->plane[plane],out+used++);
            }
            if(dn>=0){if(used>=8)return -1;out[used++]=next;}
            prev=next;dp=dn;
        }
        if(used<3)return 0;
        current=!current;count=used;
    }
    int used=0;
    for(int i=1;i+1<count;i++) {
        output[used++]=buffers[current][0];output[used++]=buffers[current][i];output[used++]=buffers[current][i+1];
    }
    return used;
}
#endif
