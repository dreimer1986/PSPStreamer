#include "cave_visual.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
int main(void) {
    assert(cave_path_shape(-1,2)==0 && cave_path_shape(2,2)==1);
    assert(fabsf(cave_path_shape(.5f,2)-.5f)<1e-6f);
    assert(fabsf(cave_path_shape(.25f,1)-.1464466094f)<1e-6f);
    assert(fabsf(cave_path_shape(.25f,.5f)-.1982233047f)<1e-6f);
    /* Four-knot nonuniform straight line must remain exactly linear. */
    CaveSpline line={.time={-2,0,3,7},.value={-.2f,0,.3f,.7f},.ready=1};
    unsigned spline_random=123;
    for(int i=0;i<=30;i++)assert(fabsf(cave_spline_sample(&line,&spline_random,i*.1f,26,.3f,1)-i*.01f)<1e-6f);
    assert(spline_random==123); /* No random churn inside an existing segment. */
    CaveSpline curve={0};
    for(int i=0;i<10000;i++) {
        float value=cave_spline_sample(&curve,&spline_random,i*.25f,26,.5f,1.05f);
        assert(isfinite(value) && fabsf(value)<2);
        for(int j=0;j<3;j++)assert(curve.time[j]<curve.time[j+1]);
    }
    struct {MdVertex v[30];unsigned guard;} out;
    for(int variant=0;variant<4;variant++)for(int mask=0;mask<256;mask++) {
        float f[8];for(int i=0;i<8;i++)f[i]=(mask&(1<<i))?(1+i*.13f):variant==3?0:-(1+variant*i*.17f);
        out.guard=0x12345678;
        int n=cave_polygonize(f,out.v,30,0,0,0);
        assert(n>=0 && n<=30 && n%3==0 && out.guard==0x12345678);
        for(int i=0;i<n;i++) {
            assert(isfinite(out.v[i].u) && isfinite(out.v[i].v));
            assert(fabsf(out.v[i].u-(.5f+out.v[i].x/12+out.v[i].z/96))<1e-6f);
            assert(fabsf(out.v[i].v-(.5f+out.v[i].y/12))<1e-6f);
            assert(out.v[i].x>=0 && out.v[i].x<=1 && out.v[i].y>=0 && out.v[i].y<=1);
            assert(out.v[i].z>=-1 && out.v[i].z<=0);
        }
        assert(cave_polygonize(f,out.v,29,0,0,0)==-1);
    }
    CaveScene *s=cave_create();assert(s && !((uintptr_t)s&63));
    while(s->paths.next<15)cave_paths_step(&s->paths);
    CavePaths paths;cave_paths_init(&paths,12345);
    CavePaths oracle;cave_paths_init(&oracle,1);oracle.seed=0;
    for(int i=0;i<CAVE_PATHS;i++)oracle.phase[i][0]=oracle.phase[i][1]=0;
    cave_paths_step(&oracle);
    /* Independent scalar reduction of the source's four terms at seed=t=i=0:
     * X={.11,.06,.11,.06}; Y={.126,.048,.070,.092}; phase residues={31,4,27,0}.
     * Weighted X=.0636455146 is clamped; Y=.0579236505 is not. */
    assert(fabsf(oracle.phase[0][0]-.06f)<1e-6f);
    assert(fabsf(oracle.phase[0][1]-.0579236505f)<1e-6f);
    /* Jitter affects odd paths only, protects the primary camera path and
     * deliberately uses the DLL's X-only distance gate. */
    CavePathFrame original={0};
    for(int i=0;i<CAVE_PATHS;i++){original.x[i]=.5f;original.y[i]=.5f;original.radius[i]=.2f;}
    original.y[1]=.9f; /* Far in Y, but source gate still admits perturbation. */
    original.x[3]=.8f; /* Beyond source X gate: unchanged. */
    CavePathFrame changed=original,unchanged=original;
    unsigned random=123,disabled=123;
    cave_paths_perturb(&changed,&random,.015f);
    cave_paths_perturb(&unchanged,&disabled,0);
    for(int i=0;i<CAVE_PATHS;i++) {
        assert(unchanged.x[i]==original.x[i] && unchanged.y[i]==original.y[i] && unchanged.radius[i]==original.radius[i]);
        if(!(i&1) || i==3)assert(changed.x[i]==original.x[i] && changed.y[i]==original.y[i] && changed.radius[i]==original.radius[i]);
        assert(fabsf(changed.x[i]-original.x[i])<=.01501f && fabsf(changed.y[i]-original.y[i])<=.01501f);
    }
    assert(changed.x[1]!=original.x[1] || changed.y[1]!=original.y[1]);
    for(int n=0;n<4000;n++) {
        float before[CAVE_PATHS][2];
        for(int i=0;i<CAVE_PATHS;i++)for(int j=0;j<2;j++)before[i][j]=paths.phase[i][j];
        cave_paths_step(&paths);
        const CavePathFrame *frame=&paths.frames[n%CAVE_PATH_CACHE];
        for(int i=0;i<CAVE_PATHS;i++) {
            assert(frame->x[i]>=0 && frame->x[i]<=1);
            assert(frame->y[i]>=0 && frame->y[i]<=1);
            assert(frame->radius[i]>=.05f && frame->radius[i]<=.5f);
            for(int j=0;j<2;j++) {
                float delta=paths.phase[i][j]-before[i][j];if(delta<-.1f)delta+=6.283185307f;
                assert(delta>=-1e-6f && delta<=(i?.120001f:.060001f));
            }
        }
        if(n>0) {
            CavePathFrame sample,derivative;
            assert(cave_paths_sample(&paths,n-.5f,&sample,&derivative));
            const CavePathFrame *previous=&paths.frames[(n-1)%CAVE_PATH_CACHE];
            for(int i=0;i<CAVE_PATHS;i++) {
                assert(fabsf(sample.x[i]-(previous->x[i]+frame->x[i])*.5f)<1e-6f);
                assert(fabsf(derivative.radius[i]-(frame->radius[i]-previous->radius[i]))<1e-6f);
            }
        }
    }
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
        float p[3]={-5.8f+(i%29)*.4f,-5.6f+(i%23)*.5f,.125f+(i%12)+(i%11)*.01f},g[3];
        cave_sample(s,p[0],p[1],p[2],g);
        for(int axis=0;axis<3;axis++) {
            float a[3],b[3];for(int j=0;j<3;j++)a[j]=b[j]=p[j];
            /* Small enough not to bridge nearby noise-clamp transitions. */
            a[axis]+=.0002f;b[axis]-=.0002f;
            float numerical=(cave_density(s,a[0],a[1],a[2])-cave_density(s,b[0],b[1],b[2]))/.0004f;
            if(!isfinite(g[axis]) || fabsf(numerical-g[axis])>=.003f) {
                fprintf(stderr,"gradient i=%d axis=%d p=%g,%g,%g analytic=%g difference=%g\n",i,axis,p[0],p[1],p[2],g[axis],numerical);
            }
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
        float x,y;cave_camera(s,s->motion.travel,&x,&y);
        assert(cave_density(s,x,y,s->motion.travel)>0);
        float view[16];cave_view(s,s->motion.travel,view);
        for(int i=0;i<3;i++)for(int j=0;j<3;j++) {
            float dot=0;for(int k=0;k<3;k++)dot+=view[k*4+i]*view[k*4+j];
            assert(fabsf(dot-(i==j?1:0))<1e-5f);
        }
        for(int i=0;i<3;i++)assert(fabsf(view[i]*x+view[4+i]*y-view[8+i]*s->motion.travel+view[12+i])<.0001f);
    }
    assert(s->motion.travel>500 && s->ready==CAVE_SLICES);
    printf("Cave: source oscillator fixture, 4000 path steps, 2100 gradient comparisons, 1024 cube cases, 1800 ticks; %d slabs, peak %d vertices/slab, allocation %zu bytes\n",s->built,peak,sizeof(*s));
    cave_destroy(s);
    return 0;
}
