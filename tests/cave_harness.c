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
    unsigned char bands[12];for(int i=0;i<12;i++)bands[i]=90;
    int peak=0;
    for(int tick=0;tick<1800;tick++) {
        int before=s->built;
        CaveSlice *slice=cave_prepare(s,bands,90,1000000ULL+tick*100000ULL);
        assert(s->built-before<=1);
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
    printf("Cave: 1024 cube cases, 1800 ticks; %d slabs, peak %d vertices/slab, allocation %zu bytes\n",s->built,peak,sizeof(*s));
    cave_destroy(s);
    return 0;
}
