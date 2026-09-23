/* SPDX-License-Identifier: GPL-2.0-or-later
 * Profile curvature controller at 0x10005d87..0x1000617a. */
#ifndef PSPSTREAMER_CAVE_BEND_H
#define PSPSTREAMER_CAVE_BEND_H
#include <math.h>
typedef struct {float x,y;} CaveBendController;
typedef struct {float rotation[9],center[3],bank_curve;int index;} CavePose;
static inline void cave_bend_rotation(CaveBendController *c,float seed,float t,float r[9]) {
    static const float amplitude[5]={.0011f,.0012f,.0013f,.0014f,.0015f};
    static const float seed_rate[5]={.67f,.92f,.37f,.11f,.51f};
    static const float rate[5]={.0268f,.0105f,.0182f,.0141f,.0221f};
    static const float power[5]={4.4f,3.9f,3.7f,4.1f,4};
    float strength=0;
    for(int i=0;i<5;i++)strength+=amplitude[i]*powf(.5f+.5f*sinf(seed*seed_rate[i]+t*rate[i]),power[i]);
    strength=fminf(.0027f,fmaxf(0,strength));
    c->x=.9f*c->x+.1f*(3*sinf(.77f*seed+.05913f*t)-2.5f*sinf(.33f*seed+.07972f*t)-1.9f*sinf(.61f*seed+.08391f*t));
    c->y=.9f*c->y+.1f*(2.1f*sinf(.53f*seed+.05253f*t)+3.2f*sinf(.17f*seed+.04316f*t)-2.1f*sinf(.81f*seed+.07336f*t));
    float axis=atan2f(c->y,c->x)-1.5707963268f;
    /* Reflect source +Z to GE -Z: quaternion axis X/Y changes sign. */
    float x=-cosf(axis)*sinf(strength*.5f),y=-sinf(axis)*sinf(strength*.5f),w=cosf(strength*.5f);
    float result[9]={1-2*y*y,2*x*y,2*y*w,2*x*y,1-2*x*x,-2*x*w,-2*y*w,2*x*w,1-2*(x*x+y*y)};
    for(int i=0;i<9;i++)r[i]=result[i];
}
static inline void cave_pose_next(const CavePose *a,const float local[9],CavePose *b) {
    for(int row=0;row<3;row++) {
        b->center[row]=a->center[row]-a->rotation[row*3+2];
        for(int col=0;col<3;col++) {
            float value=0;for(int k=0;k<3;k++)value+=a->rotation[row*3+k]*local[k*3+col];
            b->rotation[row*3+col]=value;
        }
    }
    b->index=a->index+1;
    /* Reflected local R02 = cos(direction)*sin(strength). Restore strength
     * for the original banking correction (angle never exceeds .0027). */
    float strength=acosf(fminf(1,fmaxf(-1,(local[0]+local[4]+local[8]-1)*.5f)));
    b->bank_curve=local[2]*(strength>1e-6f?strength/sinf(strength):1);
}
#endif
