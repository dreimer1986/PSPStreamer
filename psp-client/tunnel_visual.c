/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "tunnel_visual.h"
#include <math.h>

/* Seamless, deterministic, original rock-like texture. Created once, no I/O. */
void tunnel_texture(uint32_t *pixels) {
    for(int y=0;y<TUNNEL_TEXTURE;y++)for(int x=0;x<TUNNEL_TEXTURE;x++) {
        float a=x*(6.283185307f/TUNNEL_TEXTURE),b=y*(6.283185307f/TUNNEL_TEXTURE);
        float grain=sinf(a*7+b*3)*cosf(b*5-a*2);
        int shade=(int)(138+35*sinf(a*2+1.4f*sinf(b*3))+24*cosf(b*4-a)+16*grain);
        pixels[y*TUNNEL_TEXTURE+x]=0xff000000U|(unsigned)shade|((unsigned)(shade*9/10)<<8)|((unsigned)(shade*3/4)<<16);
    }
}

int tunnel_mesh(TunnelState *state,MdVertex *out,int capacity,
                const unsigned char bands[12],int level,unsigned long long now) {
    if(capacity<TUNNEL_VERTICES)return 0;
    float dt=state->previous && now>=state->previous?(now-state->previous)*.000001f:0;
    state->previous=now;
    if(dt>.1f)dt=.1f; /* No catch-up burst after menus, pause or scheduling gaps. */
    float bass=(bands[0]+bands[1]+bands[2])/300.f;
    if(bass>1)bass=1;
    if(level<0)level=0;
    if(level>100)level=100;
    float smooth=dt*8;if(smooth>1)smooth=1;
    state->bass+=(bass-state->bass)*smooth;
    state->level+=(level*.01f-state->level)*smooth;
    /* Geometry/texture periods coincide at this wrap, avoiding long-run float loss. */
    const float unit=6.283185307f/256.f;
    state->travel+=dt*(2.8f+2.2f*state->bass);
    if(state->travel>=256)state->travel-=256;
    float t=state->travel,phase=t*unit;
    float eye_x=3*sinf(phase),eye_y=2*cosf(phase*2);
    float dx=3*unit*cosf(phase),dy=-4*unit*sinf(phase*2);
    /* Single render-thread scratch, never stack or audio-worker storage. */
    static MdVertex rings[TUNNEL_RINGS][TUNNEL_SIDES+1];
    for(int r=0;r<TUNNEL_RINGS;r++) {
        float world=r?floorf(t)+r:t,z=world-t+.25f;
        float bend_x=3*sinf(world*unit)-eye_x-dx*z;
        float bend_y=2*cosf(world*unit*2)-eye_y-dy*z;
        float fade=1-z/28;if(fade<0)fade=0;
        for(int s=0;s<=TUNNEL_SIDES;s++) {
            float a=(s%TUNNEL_SIDES)*(6.283185307f/TUNNEL_SIDES);
            float radius=2.6f+.25f*sinf(world*unit*7+a*3)+.16f*cosf(world*unit*11-a*2);
            radius*=1+.045f*state->bass;
            float light=(.50f+.25f*cosf(a-.7f))*fade;
            unsigned red=(unsigned)(255*light),green=(unsigned)((145+70*state->level)*light);
            unsigned blue=(unsigned)((105+100*state->bass)*light);
            rings[r][s]=(MdVertex){s*(1.f/TUNNEL_SIDES),world*.25f,
                0xff000000U|red|(green<<8)|(blue<<16),
                bend_x+radius*cosf(a),bend_y+radius*sinf(a),-z};
        }
    }
    /* Far-to-near opaque tube bands: bounded draw cost, no depth-buffer allocation. */
    int n=0;
    for(int r=TUNNEL_RINGS-2;r>=0;r--)for(int s=0;s<TUNNEL_SIDES;s++) {
        out[n++]=rings[r][s];out[n++]=rings[r+1][s];out[n++]=rings[r+1][s+1];
        out[n++]=rings[r][s];out[n++]=rings[r+1][s+1];out[n++]=rings[r][s+1];
    }
    return n;
}
