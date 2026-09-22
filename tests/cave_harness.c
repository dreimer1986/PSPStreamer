#include "cave_visual.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
int main(void) {
    struct {MdVertex v[30];unsigned guard;} out;
    for(int variant=0;variant<4;variant++)for(int mask=0;mask<256;mask++) {
        float f[8];for(int i=0;i<8;i++)f[i]=(mask&(1<<i))?(1+i*.13f):variant==3?0:-(1+variant*i*.17f);
        out.guard=0x12345678;
        int n=cave_polygonize(f,out.v,30,0,0,0);
        assert(n>=0 && n<=30 && n%3==0 && out.guard==0x12345678);
        for(int i=0;i<n;i++) {
            assert(isfinite(out.v[i].u) && isfinite(out.v[i].v));
            assert(out.v[i].x>=0 && out.v[i].x<=1 && out.v[i].y>=0 && out.v[i].y<=1);
            assert(out.v[i].z>=-1 && out.v[i].z<=0);
        }
        assert(cave_polygonize(f,out.v,29,0,0,0)==-1);
    }
    CaveScene *s=cave_create();assert(s && !((uintptr_t)s&63));
    /* Recovered octave matrices must preserve lengths; orientation is also
     * checked at known angles rather than just accepting any rotation. */
    float m[9];cave_noise_rotation(m,0,0);
    for(int i=0;i<9;i++)assert(fabsf(m[i]-(i%4==0?1:0))<1e-6f);
    cave_noise_rotation(m,0,1.570796327f);
    assert(fabsf(m[2]+1)<1e-6f && fabsf(m[6]-1)<1e-6f && m[4]==1);
    for(int octave=0;octave<3;octave++) {
        const float *r=s->noise_matrix[octave];
        for(int i=0;i<3;i++)for(int j=0;j<3;j++) {
            float dot=0;for(int k=0;k<3;k++)dot+=r[i*3+k]*r[j*3+k];
            assert(fabsf(dot-(i==j?1:0))<1e-5f);
        }
    }
    /* Independent central difference oracle checks radial Z derivatives,
     * rotated noise chain rule, clamp behavior and negative lattice positions. */
    for(int i=0;i<700;i++) {
        float p[3]={-5.8f+(i%29)*.4f,-5.6f+(i%23)*.5f,i*.017f},g[3];
        cave_sample(s,p[0],p[1],p[2],g);
        for(int axis=0;axis<3;axis++) {
            float a[3],b[3];for(int j=0;j<3;j++)a[j]=b[j]=p[j];
            a[axis]+=.002f;b[axis]-=.002f;
            float numerical=(cave_density(s,a[0],a[1],a[2])-cave_density(s,b[0],b[1],b[2]))/.004f;
            assert(isfinite(g[axis]) && fabsf(numerical-g[axis])<.003f);
        }
    }
    uint32_t pixels[CAVE_TEXTURE*CAVE_TEXTURE];cave_texture(pixels);
    assert(pixels[0]!=pixels[25] && (pixels[0]>>24)==255);
    unsigned char bands[12];for(int i=0;i<12;i++)bands[i]=90;
    int peak=0;
    for(int tick=0;tick<1800;tick++) {
        int before=s->built;
        CaveSlice *slice=cave_prepare(s,bands,90,1000000ULL+tick*100000ULL);
        assert(s->built-before<=1);
        assert(s->sampled_planes==s->built+1);
        if(slice) {
            assert(!((uintptr_t)slice->vertices&63));
            assert(slice->count>0 && slice->count<=CAVE_MAX_VERTICES);
            if(slice->count>peak)peak=slice->count;
            for(int i=0;i<slice->count;i++)assert(isfinite(slice->vertices[i].z));
        }
        if(s->ready>=8)assert(s->motion.travel<s->next-5);
        float x,y;cave_camera(s->motion.travel,&x,&y);
        assert(cave_density(s,x,y,s->motion.travel)>0);
    }
    assert(s->motion.travel>500 && s->ready==CAVE_SLICES);
    printf("Cave: rotations, 2100 gradient comparisons, 1024 cube cases, 1800 ticks; %d slabs, peak %d vertices/slab, allocation %zu bytes\n",s->built,peak,sizeof(*s));
    cave_destroy(s);
    return 0;
}
