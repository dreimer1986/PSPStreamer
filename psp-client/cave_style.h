/* SPDX-License-Identifier: GPL-2.0-or-later
 * Recovered Monkey color envelopes and two directional-light construction.
 * See MONKEY_GEOMETRY.md for the remaining ambient/material adaptations. */
#ifndef PSPSTREAMER_CAVE_STYLE_H
#define PSPSTREAMER_CAVE_STYLE_H
#include <math.h>
#include "cave_paths.h"
typedef struct {float color[2][3],direction[2][3],ambient;} CaveLight;
static inline float cave_unit(float x){return fmaxf(0,fminf(1,x));}
/* Background/fog palette, NOT Hair color: 0x10003a10, 0x10005b62.
 * The source deliberately raises dark palettes before clearing the frame. */
static inline void cave_background_update(float rgb[3],float seed,const float phase[3]) {
    const float base[3]={174,154,134},range[3]={121.8f,111.65f,101.5f},rate[3]={.17f,.13f,.29f};
    for(int k=0;k<3;k++)rgb[k]=rgb[k]*.96f+.04f*(base[k]+range[k]*sinf(phase[k]*.013f+seed*rate[k]));
    float brightness=rgb[0]+rgb[1]+rgb[2]*.88f;
    if(brightness>0 && brightness<440) {
        for(int k=0;k<3;k++)rgb[k]*=440/brightness;
        for(int k=0;k<3;k++)if(rgb[k]>255) {
            float extra=(rgb[k]-255)*.5f;rgb[k]=255;
            rgb[(k+1)%3]+=extra;rgb[(k+2)%3]+=extra;
        }
    }
    for(int k=0;k<3;k++)rgb[k]=fmaxf(0,fminf(255,rgb[k]));
}
static inline unsigned cave_background_color(const float rgb[3],int fog,int black) {
    /* 0x10006e83: black only when fog is OFF and the black-material flag ON.
     * GE takes ABGR, whereas the original Direct3D clear takes ARGB. */
    if(!fog && black)return 0xff000000U;
    return 0xff000000U|(unsigned)rgb[0]|((unsigned)rgb[1]<<8)|((unsigned)rgb[2]<<16);
}
static inline float cave_fog_end(float seed,float t,float horizon) {
    /* Original linear vertex fog starts at zero and ends at 29.04..33.
     * Scale its depth envelope to the PSP's shorter prepared horizon. */
    float end=31.02f+.99f*(sinf(seed*.29f+t*.01513f)+sinf(seed*.66f+t*.0219f));
    return end*(horizon/33);
}
/* 0x100065a0..0x100066e3, selected movement=1 and neutral projection gate.
 * The caller still bounds advancement to geometry actually in the cache. */
static inline float cave_forward_step(float dt,float impulse) {
    if(dt<=0)return 0;
    float fps=1/dt,rate=fmaxf(8,fminf(50,fps))*.04f;
    if(fps>25)rate=powf(rate,1.2f);
    return fminf(4,(12.2f*dt*(1+impulse)+.1f*impulse)*(.6f+.4f*rate));
}
/* Original texture-factor colors, evaluated at render time (0x10008080..
 * 0x100083b3). They replace, rather than modulate, vertex diffuse colors. */
static inline unsigned cave_effect_color(float seed,float t,int black,int hair,int transparent) {
    float base,amplitude,shape;
    const float wire_seed[3]={.42f,.17f,.79f},wire_rate[3]={.00312f,.00279f,.00253f};
    const float hair_seed[3]={.32f,.57f,.39f},hair_rate[3]={.00275f,.00252f,.00214f};
    unsigned result=hair&&transparent?(black?0x80000000U:0x58000000U):0xff000000U;
    if(hair){base=.05f+.5f*sinf(seed*.63f+t*.00229f);amplitude=.02f;shape=3;}
    else {base=(black?.9f:.1f)+(black?.2f:.5f)*sinf(seed*.13f+t*.00232f);amplitude=black?.4f:.08f;shape=4;}
    for(int k=0;k<3;k++) {
        float x=base+amplitude*sinf(seed*(hair?hair_seed[k]:wire_seed[k])+t*(hair?hair_rate[k]:wire_rate[k]));
        result|=(unsigned)(255*cave_path_shape(x,shape))<<(8*k);
    }
    return result;
}
static inline void cave_lighting(CaveLight *light,float seed,float t) {
    /* 0x10004cef..0x10004fa3: channel envelopes, 6% gray mixing,
     * limit sum of deviations to .35, then complementary second light. */
    float r=.5f+.1f*sinf(seed*.41f+t*.00221f)+.4f*sinf(seed*.14f+t*.00317f);
    float g=.5f+.1f*sinf(seed*.32f+t*.00411f)+.4f*sinf(seed*.94f+t*.00197f);
    float b=.5f+.1f*sinf(seed*.79f+t*.00297f)+.4f*sinf(seed*.47f+t*.00247f);
    r*=r;b=sqrtf(fmaxf(0,b));
    float gray=(r+g+b)/3;
    float c[3]={r*.94f+gray*.06f,g*.94f+gray*.06f,b*.94f+gray*.06f};
    float deviation=fabsf(c[0]-gray)+fabsf(c[1]-gray)+fabsf(c[2]-gray);
    float scale=deviation>.35f?.35f/deviation:1;
    for(int i=0;i<3;i++){light->color[0][i]=cave_unit(gray+(c[i]-gray)*scale);light->color[1][i]=1-light->color[0][i];}
    /* 0x10004fa7..0x100051e8: bounded secondary chroma disturbance. */
    float envelope=1.4f*powf(cave_unit(.5f+.23f*sinf(seed*.35f+t*.000997f)+.27f*sinf(seed*.53f+t*.001127f)),3.4f);
    float delta[3]={envelope*sinf(seed*.42f+t*.002317f),envelope*sinf(seed*.26f+t*.002197f),envelope*sinf(seed*.73f+t*.001747f)};
    for(int i=0;i<3;i++) {
        float base=light->color[1][i];
        if(base+delta[i]>1)delta[i]=1-base;
        if(base-delta[i]<0)delta[i]=-base; /* Preserve source branch, not a symmetric clamp. */
    }
    float common=.3f*(delta[0]+delta[1]+delta[2]);
    for(int i=0;i<3;i++)light->color[1][i]=cave_unit(light->color[1][i]+delta[i]-common);
    /* 0x100051e8..0x100052da: two related spherical light directions. */
    float a=4.71f+.85f*sinf(seed*.371f+t*.00611f);
    float e=1.67f*sinf(seed*.491f+t*.00494f);
    for(int i=0;i<2;i++) {
        light->direction[i][0]=cosf(a)*cosf(e);
        light->direction[i][1]=sinf(a)*cosf(e);
        light->direction[i][2]=sinf(e);
        a+=.5f+.6f*sinf(seed*.124f+t*.00773f);
        e+=.7f*sinf(seed*.782f+t*.00903f);
    }
    /* 0x100052de..0x100053af, ordinary (non-extra-material) branch. */
    float ambient=.5f+.14f*sinf(seed*.998f+t*.0041f)+.11f*sinf(seed*.763f+t*.00706f)
        +.12f*sinf(seed*.334f+t*.00981f)+.13f*sinf(seed*.531f+t*.01303f);
    light->ambient=.4f+.3f*(2*cave_path_shape(ambient,-1.8f)-1);
}
static inline unsigned cave_shade_material(const CaveLight *a,const CaveLight *b,float t,float nx,float ny,float nz,const float material[3]) {
    float length=sqrtf(nx*nx+ny*ny+nz*nz);
    float normal[3]={0,0,0};
    if(length>1e-6f){normal[0]=nx/length;normal[1]=ny/length;normal[2]=nz/length;}
    float color[3]={0,0,0};
    for(int l=0;l<2;l++) {
        float dot=0;
        for(int j=0;j<3;j++)dot+=normal[j]*(a->direction[l][j]+t*(b->direction[l][j]-a->direction[l][j]));
        /* Source scales directions by 1.04, then clamps signed dot +
         * varying ambient to [.17,1], no fabs(). */
        float strength=fmaxf(.17f,fminf(1,dot*1.04f+a->ambient+t*(b->ambient-a->ambient)));
        for(int j=0;j<3;j++)color[j]+=strength*(a->color[l][j]+t*(b->color[l][j]-a->color[l][j]));
    }
    return 0xff000000U|(unsigned)(255*cave_unit(color[0]*material[0]))|((unsigned)(255*cave_unit(color[1]*material[1]))<<8)|((unsigned)(255*cave_unit(color[2]*material[2]))<<16);
}
static inline unsigned cave_shade(const CaveLight *a,const CaveLight *b,float t,float nx,float ny,float nz) {
    const float material[3]={1,1,1};return cave_shade_material(a,b,t,nx,ny,nz,material);
}
#endif
