/* SPDX-License-Identifier: GPL-2.0-or-later
 * Recovered scene controller; addresses refer to the audited Monkey 1.0 DLL. */
#ifndef PSPSTREAMER_CAVE_CONTROL_H
#define PSPSTREAMER_CAVE_CONTROL_H
#include <math.h>
#include <stdint.h>
/* 0x10012aad: private state, never perturb the player's libc RNG. */
static inline unsigned cave_random(unsigned *state) {
    *state=(uint32_t)*state*214013U+2531011U;
    return (*state>>16)&32767U;
}
/* 0x10006c9d..0x10006d8c: the source varies this continuously, it is not
 * a fixed movement preset. Bounds follow directly from the sine envelopes. */
static inline float cave_movement(float seed,float t) {
    float x=.5f*(1+.32f*sinf(t*.00207f-seed*.3317f)
        +.25f*sinf(t*.00331f+seed*.2172f)+.43f*sinf(t*.00253f+seed*.7631f));
    x=powf(fmaxf(0,fminf(1,x)),.75f);
    x=fminf(1,1.2f*(.6f*(.5f-.5f*cosf(x*3.141592654f))+.4f*x));
    return exp2f(6*x-4);
}
/* 0x10006d92..0x10006e16: spatial jitter, separate from wall noise. */
static inline float cave_roughness(float seed,float t) {
    return .005f*(1+.22f*sinf(t*.00317f-seed*.2356f)
        +.15f*sinf(t*.00261f+seed*.5692f)+.63f*sinf(t*.00373f+seed*.7513f));
}
/* 0x1000759e..0x100075cc; source FOV multiplier defaults to one. */
static inline float cave_fov(float seed,float t) {
    return .195f+.04f*sinf(seed*.131f+t*.0075f);
}
/* D3D projection at 0x10007603 and 0x1000a150. XY world coordinates are
 * six times the source coordinates, hence divide both focal scales by six. */
static inline void cave_projection(float seed,float t,int width,int height,float *x,float *y) {
    float fov=cave_fov(seed,t),aspect=(float)height/width;
    float horizontal=aspect<1?fov:fov/aspect;
    float vertical=aspect<1?fov*aspect:fov;
    *x=1/(6*tanf(horizontal*.5f));*y=1/(6*tanf(vertical*.5f));
}
/* 0x1000671e..0x100068ee. Probability and fades follow distance advanced,
 * not generated-slab count; remember the midpoint even for large steps. */
static inline void cave_texture_advance(unsigned *random,int indices[2],float progress[2],int changed[2],float step,int multitexture) {
    if(!(step>0))return;
    unsigned period=(unsigned)fmaxf(1,(multitexture?650.f:400.f)/step);
    if(!multitexture) {
        if(cave_random(random)%period==0)indices[0]=cave_random(random)%5;
        return;
    }
    for(int k=0;k<2;k++)if(progress[0]<0 && progress[1]<0 && cave_random(random)%period==0) {
        progress[k]=.01f;changed[k]=0;
    }
    for(int k=0;k<2;k++) {
        if(progress[k]>34 && !changed[k]) {
            indices[k]=cave_random(random)%(k?2:5);changed[k]=1;
        }
        if(progress[k]>68)progress[k]=-1;
        if(progress[k]>=0)progress[k]+=step;
    }
}
#endif
