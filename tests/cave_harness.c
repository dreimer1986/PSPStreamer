#include "cave_visual.h"
#include "cave_clip.h"
#include "cave_topology.h"
#include "cave_style.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "cave_ship_data.h"
static void test_ship_wall(void) {
    CaveScene *s=cave_create();assert(s);
    s->ready=16;s->next=16;s->pose_next=18;s->paths.next=18;
    for(int i=0;i<CAVE_PATH_CACHE;i++) {
        CavePathFrame *p=&s->paths.frames[i];p->index=i;
        s->poses[i].index=i;s->poses[i].center[2]=-i;
        memset(s->poses[i].rotation,0,sizeof(s->poses[i].rotation));
        s->poses[i].rotation[0]=s->poses[i].rotation[4]=s->poses[i].rotation[8]=1;
        for(int j=0;j<CAVE_PATHS;j++){p->x[j]=p->y[j]=.5f;p->radius[j]=.7f;}
    }
    CaveSlice *sl=&s->slices[0];sl->index=0;sl->count=6;
    const float yz[6][2]={{-10,-30},{10,-30},{10,5},{-10,-30},{10,5},{-10,5}};
    for(int i=0;i<6;i++){sl->vertices[i].x=.5f;sl->vertices[i].y=yz[i][0];sl->vertices[i].z=yz[i][1];}
    sl->minimum[0]=sl->maximum[0]=.5f;sl->minimum[1]=-10;sl->maximum[1]=10;sl->minimum[2]=-30;sl->maximum[2]=5;
    float n[3];assert(!cave_ship_contact(s,0,0,0,n));
    assert(!cave_ship_contact(s,.2f,0,0,n)); /* former oversized sphere hit */
    assert(!cave_ship_contact(s,.5f-CAVE_SHIP_HALF_X-.001f,0,0,n));
    assert(cave_ship_contact(s,.5f-CAVE_SHIP_HALF_X+.001f,0,0,n) && n[0]<-.99f);
    /* Banking turns the thin vertical extent toward this wall. */
    s->flight_roll=1.570796327f;
    assert(!cave_ship_contact(s,.5f-CAVE_SHIP_HALF_Y-.001f,0,0,n));
    assert(cave_ship_contact(s,.5f-CAVE_SHIP_HALF_Y+.001f,0,0,n) && n[0]<-.99f);
    s->flight_roll=0;
    cave_flight_input(s,1,255,128,0,0);
    s->flight_x=s->flight_y=s->flight_roll=0;s->flight_yaw=.85f;s->flight_initialized=1;
    s->motion.previous=1000000;unsigned char bands[12]={0};
    cave_options.flight_sensitivity=100;cave_options.flight_inertia=0;
    for(int i=1;i<=15;i++) {
        cave_prepare(s,bands,0,1000000+i*10000);
        assert(s->flight_x<.5f);
        assert(!cave_ship_contact(s,s->flight_x,s->flight_y,s->motion.travel,n));
    }
    assert(s->motion.travel>.5f); /* tangential progress, not a wall freeze */
    s->flight_yaw=0;cave_options.flight_inertia=0;
    cave_prepare(s,bands,0,1160000);float quick=s->flight_yaw;
    s->flight_yaw=0;cave_options.flight_inertia=100;
    cave_prepare(s,bands,0,1170000);assert(s->flight_yaw<quick*.2f);
    cave_options.flight_sensitivity=50;cave_options.flight_inertia=65;
    cave_destroy(s);
}
int main(void) {
    test_ship_wall();
    /* Isolated DLL execution: 0x10006c9d and 0x1000759e. */
    const float scene_fixture[][5]={{0,0,1.419137120f,.005f,.195f},
        {313.45f,100,2.093897343f,.005565370f,.161976412f},
        {10,2000,4,.005544009f,.172347113f},
        {996.99f,10000,.676751614f,.006741541f,.155567393f}};
    for(unsigned i=0;i<sizeof(scene_fixture)/sizeof(scene_fixture[0]);i++) {
        const float *f=scene_fixture[i];
        assert(fabsf(cave_movement(f[0],f[1])-f[2])<.00005f);
        assert(fabsf(cave_roughness(f[0],f[1])-f[3])<.000001f);
        assert(fabsf(cave_fov(f[0],f[1])-f[4])<.000001f);
        float x,y;cave_projection(f[0],f[1],480,272,&x,&y);
        assert(isfinite(x)&&isfinite(y)&&x>0&&y>x);
    }
    unsigned rng_state=1;
    const unsigned random_fixture[]={41,18467,6334,26500,19169};
    for(unsigned i=0;i<5;i++)assert(cave_random(&rng_state)==random_fixture[i]);
    int texture[2]={0,0},changed_texture[2]={0,0};float transition[2]={33.5f,-1};
    cave_texture_advance(&rng_state,texture,transition,changed_texture,2,1);
    assert(!changed_texture[0] && transition[0]==35.5f);
    cave_texture_advance(&rng_state,texture,transition,changed_texture,2,1);
    assert(changed_texture[0] && transition[0]==37.5f && texture[0]<5);
    unsigned retained=rng_state;
    cave_texture_advance(&rng_state,texture,transition,changed_texture,2,1);
    assert(rng_state==retained); /* Exactly one midpoint replacement. */
    transition[0]=69;
    cave_texture_advance(&rng_state,texture,transition,changed_texture,2,1);
    assert(transition[0]<0);
    /* Isolated original x86 palette/brightness routines, not guessed colors. */
    const float seeds[3]={0,313.45f,10};
    const float phases[3][3]={{0,0,0},{100,200,300},{20,30,40}};
    float palette[3][3]={{0,0,0},{120,150,180},{250,40,20}};
    const float expected_palette[3][3]={{171.689987f,151.955505f,132.221024f},
        {122.145760f,153.143890f,187.170853f},{255,111.933472f,76.921654f}};
    for(int i=0;i<3;i++) {
        cave_background_update(palette[i],seeds[i],phases[i]);
        for(int k=0;k<3;k++)assert(fabsf(palette[i][k]-expected_palette[i][k])<.001f);
        assert(cave_background_color(palette[i],0,0)!=0xff000000);
        assert(cave_background_color(palette[i],0,1)==0xff000000);
        assert(cave_background_color(palette[i],1,1)==cave_background_color(palette[i],1,0));
    }
    assert(fabsf(cave_fog_end(0,0,33)-31.02f)<1e-5f);
    /* Newest slab's upper edge must use its curved pose even before the
     * following profile exists. It must not move when that pose is added. */
    CaveScene *boundary=cave_create();assert(boundary);
    const float quarter_turn[9]={0,0,1,0,1,0,-1,0,0};
    memcpy(boundary->poses[1].rotation,quarter_turn,sizeof(quarter_turn));
    boundary->poses[1].center[0]=2;boundary->poses[1].center[1]=3;boundary->poses[1].center[2]=-4;
    boundary->poses[1].index=1;boundary->poses[2].index=-1;
    float edge_before[3],edge_after[3];
    cave_world_point(boundary,3,1,1,edge_before);
    assert(fabsf(edge_before[0]-12)<1e-6f && fabsf(edge_before[1]-19)<1e-6f && fabsf(edge_before[2]+4.5f)<1e-6f);
    boundary->poses[2]=boundary->poses[1];boundary->poses[2].index=2;boundary->poses[2].center[2]-=1;
    cave_world_point(boundary,3,1,1,edge_after);
    assert(!memcmp(edge_before,edge_after,sizeof(edge_before)));
    cave_destroy(boundary);
    /* Quaternion fixtures from the original 0x10005d87..0x1000617a block,
     * including retained direction state, not an independent sine guess. */
    CaveBendController bend={0};
    const float bend_time[5]={0,1,2,3,100};
    const float source_q[5][4]={{0,-.000203953314f,0,1},
        {-.000108400593f,-.000426777522f,0,.999999881f},
        {-.000125311970f,-.000394137925f,0,.999999940f},
        {-.000130937959f,-.000365165120f,0,.999999940f},
        {.000078666082f,-.000120637247f,0,1}};
    for(int i=0;i<5;i++) {
        float r[9];cave_bend_rotation(&bend,i?313.45f:0,bend_time[i],r);
        float x=-source_q[i][0],y=-source_q[i][1],w=source_q[i][3];
        float expected[9]={1-2*y*y,2*x*y,2*y*w,2*x*y,1-2*x*x,-2*x*w,-2*y*w,2*x*w,1-2*(x*x+y*y)};
        for(int k=0;k<9;k++)assert(fabsf(r[k]-expected[k])<.000002f);
    }
    CaveMotion motion={.direction=1};
    assert(cave_beat_response(&motion,8,0,.1f,1)==1);
    assert(cave_beat_response(&motion,8,9,.1f,1)==0 && fabsf(motion.spin+.056f)<1e-6f);
    cave_beat_response(&motion,8,60,.1f,1);assert(fabsf(motion.spin-.056f)<1e-6f);
    cave_beat_response(&motion,8,61,.1f,1);assert(motion.forward==2);
    cave_beat_response(&motion,8,0,1.f/30,0);
    assert(fabsf(motion.forward-2*.988f)<1e-6f && fabsf(motion.spin-.056f*.989f)<1e-6f);
    float identity[16]={1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
    CaveClip clip;cave_clip_init(&clip,identity,1,1);
    MdVertex triangle[3]={{0,0,0xff000000,-.5f,-.5f,-1},
        {1,0,0xffffffff,.5f,-.5f,-1},{.5f,1,0xff808080,20,1,.1f}};
    MdVertex clipped[CAVE_CLIP_VERTICES];
    int n=cave_clip_triangle(&clip,triangle,clipped);
    assert(n>=3 && n<=CAVE_CLIP_VERTICES && n%3==0);
    for(int i=0;i<n;i++) {
        for(int p=0;p<CAVE_CLIP_PLANES;p++)assert(cave_clip_distance(clip.plane[p],clipped+i)>-.00001f);
        assert(clipped[i].u>=0 && clipped[i].u<=1 && clipped[i].v>=0 && clipped[i].v<=1);
        assert((clipped[i].color>>24)==255);
    }
    for(int i=0;i<3;i++)triangle[i].z=1;
    assert(cave_clip_triangle(&clip,triangle,clipped)==0);
    assert(cave_path_shape(-1,2)==0 && cave_path_shape(2,2)==1);
    assert(fabsf(cave_path_shape(.5f,2)-.5f)<1e-6f);
    assert(fabsf(cave_path_shape(.25f,1)-.1464466094f)<1e-6f);
    assert(fabsf(cave_path_shape(.25f,.5f)-.1982233047f)<1e-6f);
    assert(fabsf(cave_path_shape(.25f,-1)-1.f/3)<1e-6f);
    assert(fabsf(cave_path_shape(cave_path_shape(.2f,1),-1)-.2f)<1e-6f);
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
        int expected=0;while(expected<15 && cave_topology[255-mask][expected]>=0)expected++;
        assert(n>=0 && n<=15 && n%3==0 && out.guard==0x12345678);
        if(variant!=3)assert(n==expected);
        for(int i=0;i<n;i++) {
            assert(isfinite(out.v[i].u) && isfinite(out.v[i].v));
            assert(fabsf(out.v[i].u-(.5f+out.v[i].x/12+out.v[i].z/96))<1e-6f);
            assert(fabsf(out.v[i].v-(.5f+out.v[i].y/12))<1e-6f);
            assert(out.v[i].x>=0 && out.v[i].x<=1 && out.v[i].y>=0 && out.v[i].y<=1);
            assert(out.v[i].z>=-1 && out.v[i].z<=0);
        }
        if(expected)assert(cave_polygonize(f,out.v,expected-1,0,0,0)==-1);
    }
    CaveLight light[2];cave_lighting(light,0,0);cave_lighting(light+1,0,1);
    /* Independent source-envelope fixture before and after gray/chroma mixing. */
    float raw[3]={.25f,.5f,sqrtf(.5f)},gray=(raw[0]+raw[1]+raw[2])/3;
    float dev=0;for(int j=0;j<3;j++)dev+=fabsf(.94f*(raw[j]-gray));
    for(int j=0;j<3;j++)assert(fabsf(light->color[0][j]-(gray+.94f*(raw[j]-gray)*.35f/dev))<1e-6f);
    for(int i=0;i<500;i++) {
        cave_lighting(light,313.45f,i*7.3f);cave_lighting(light+1,313.45f,i*7.3f+1);
        assert(light->ambient>=.1f && light->ambient<=.7f);
        for(int l=0;l<2;l++) {
            float length=0;for(int j=0;j<3;j++) {
                assert(light->color[l][j]>=0 && light->color[l][j]<=1);
                length+=light->direction[l][j]*light->direction[l][j];
            }
            assert(fabsf(length-1)<1e-5f);
        }
        assert((cave_shade(light,light+1,.4f,1,2,3)>>24)==255);
    }
    CaveScene *fixture=cave_create();assert(fixture);
    for(int f=0;f<2;f++) {
        memset(&fixture->paths.frames[f],0,sizeof(CavePathFrame));fixture->paths.frames[f].index=f;
        for(int i=0;i<CAVE_PATHS;i++)fixture->paths.frames[f].radius[i]=.05f;
        fixture->paths.frames[f].x[0]=f?.5f:.49f;fixture->paths.frames[f].y[0]=f?.5f:.52f;
        fixture->paths.frames[f].radius[0]=.3f;
    }
    float material[4]={50,100,200,1};memcpy(fixture->material[0],material,sizeof(material));
    /* Outputs captured from isolated original routine 0x10002300 in Unicorn,
     * source Z reflected to our forward fraction. No plugin code is shipped. */
    const float source_normals[3][3]={{.706753492f,.706753492f,.031607587f},
        {.769213438f,.638592601f,.022587663f},{.894427001f,.447213948f,0}};
    const float fraction[3]={1,.7f,0};
    for(int i=0;i<3;i++) {
        float rgba[4],normal[3];cave_material_sample(fixture,0,.55f,.55f,fraction[i],rgba,normal);
        for(int k=0;k<4;k++)assert(fabsf(rgba[k]-material[k])<.00003f);
        for(int k=0;k<3;k++)assert(fabsf(normal[k]-source_normals[i][k])<.000003f);
    }
    cave_destroy(fixture);
    CaveScene *s=cave_create();assert(s && !((uintptr_t)s&63));
    while(s->paths.next<15)cave_paths_step(&s->paths);
    CavePaths paths;cave_paths_init(&paths,12345);
    /* Original scene initialization consumes the Hair random pool before
     * the first path update. Full-path fixture includes spline and jitter. */
    CavePaths reference;cave_paths_init(&reference,12345);
    for(int i=0;i<2048;i++)cave_random(&reference.random);
    const float path_reference[6][7]={
        {0,.367188334f,.244918227f,.339185476f,.705218792f,.274849981f,.285253465f},
        {1,.366480261f,.244485527f,.338972628f,.704932630f,.278604418f,.285040617f},
        {2,.365912884f,.244245291f,.338758141f,.701364458f,.285134673f,.284826100f},
        {32,.366929650f,.274650574f,.331602097f,.343273133f,.758047462f,.278256238f},
        {64,.358497292f,.330519795f,.322636455f,.611044586f,.247718543f,.268704414f},
        {127,.301414162f,.441051245f,.302317441f,.696015179f,.496240735f,.248385429f}};
    for(int i=0,next=0;i<128;i++) {
        cave_paths_step(&reference);const CavePathFrame *p=&reference.frames[i%CAVE_PATH_CACHE];
        if(i!=(int)path_reference[next][0])continue;
        for(int k=0;k<2;k++) {
            int at=k?3:0;const float *f=path_reference[next]+1+k*3;
            assert(fabsf(p->x[at]-f[0])<.00005f);
            assert(fabsf(p->y[at]-f[1])<.00005f);
            assert(fabsf(p->radius[at]-f[2])<.00005f);
        }
        next++;
    }
    CavePaths oracle;cave_paths_init(&oracle,1);oracle.seed=0;
    for(int i=0;i<CAVE_PATHS;i++)oracle.phase[i][0]=oracle.phase[i][1]=0;
    cave_paths_step(&oracle);
    /* Independent scalar reduction of the source's four terms at seed=t=i=0:
     * X={.11,.06,.11,.06}; Y={.126,.048,.070,.092}; phase residues={31,4,27,0}.
     * Weighted X=.0636455146 is clamped; Y=.0579236505 is not. */
    assert(fabsf(oracle.phase[0][0]-.06f)<1e-6f);
    assert(fabsf(oracle.phase[0][1]-fminf(.06f,.0579236505f*cave_movement(0,0)))<1e-6f);
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
    cave_options.noise=16;
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
    cave_options.noise=0;
    uint32_t pixels[CAVE_TEXTURE*CAVE_TEXTURE];cave_texture(pixels);
    assert(pixels[0]!=pixels[25] && (pixels[0]>>24)==255);
    unsigned char bands[12];for(int i=0;i<12;i++)bands[i]=90;
    int peak=0,clipped_visible=0;
    for(int tick=0;tick<1800;tick++) {
        int before=s->built;
        float old_bank=s->motion.bank,old_bass=s->motion.bass;
        CaveSlice *slice=cave_prepare(s,bands,90,1000000ULL+tick*100000ULL);
        assert(isfinite(s->motion.bank) && fabsf(s->motion.bank)<1.571f);
        /* The source step is now movement/FOV-dependent; a four-profile
         * source cap gives the conservative retention bound for every frame. */
        float bank_retention=powf(.43f+.5f*powf(.86f,4),1.4f);
        assert(fabsf(s->motion.bank-old_bank)<=3.142f*(1-bank_retention)+.00001f);
        if(tick)assert(fabsf(s->motion.bass-(.9f+(old_bass-.9f)*expf(-.2f)))<1e-6f);
        assert(s->built-before<=1);
        assert(s->sampled_planes==s->built+1);
        if(slice) {
            assert(!((uintptr_t)slice->vertices&63));
            assert(slice->count>0 && slice->count<=CAVE_MAX_VERTICES);
            if(slice->count>peak)peak=slice->count;
            for(int i=0;i<slice->count;i++)assert(isfinite(slice->vertices[i].z));
            /* A triangle stays inside one lofted grid cell. With normalized
             * XY mapped by six, a rigidly rotated cell plus the bounded bend
             * cannot have a 6.5-unit edge. Mixed straight/curved endpoints
             * produced arbitrarily long screen-filling triangles instead. */
            for(int i=0;i<slice->count;i+=3)for(int k=0;k<3;k++) {
                const MdVertex *a=slice->vertices+i+k,*b=slice->vertices+i+(k+1)%3;
                float dx=a->x-b->x,dy=a->y-b->y,dz=a->z-b->z;
                assert(dx*dx+dy*dy+dz*dz<6.5f*6.5f);
            }
        }
        if(s->ready>=8)assert(s->motion.travel<s->next-5);
        /* Forward prebuilding must not overwrite still-visible rear slabs. */
        int first=(int)floorf(s->motion.travel)-CAVE_HISTORY;
        if(first<0)first=0;
        for(int index=first;index<s->next;index++) {
            assert(s->slices[index%CAVE_SLICES].index==index);
            assert(s->slices[index%CAVE_SLICES].count>0);
        }
        assert(s->next<=(int)floorf(s->motion.travel)+CAVE_AHEAD);
        float x,y;cave_camera(s,s->motion.travel,&x,&y);
        assert(cave_density(s,x,y,s->motion.travel)>0);
        float view[16];cave_view(s,s->motion.travel,view);
        assert(s->motion.phase>=0 && s->motion.phase<6.284f && s->motion.pulse>=0 && s->motion.pulse<=1);
        if(tick%10==0) {
            cave_clip_init(&clip,view,.7f,1.25f);
            for(int slab=0;slab<CAVE_SLICES;slab++) {
                const CaveSlice *section=&s->slices[slab];
                for(int at=0;at<section->count;at+=3) {
                    const MdVertex *v=section->vertices+at;
                    if(!(cave_clip_mask(&clip,v)|cave_clip_mask(&clip,v+1)|cave_clip_mask(&clip,v+2)))continue;
                    int count=cave_clip_triangle(&clip,v,clipped);
                    assert(count>=0 && count<=CAVE_CLIP_VERTICES && count%3==0);
                    if(count)clipped_visible++;
                    for(int j=0;j<count;j++)for(int p=0;p<CAVE_CLIP_PLANES;p++) {
                        float distance=cave_clip_distance(clip.plane[p],clipped+j);
                        if(distance<=-.0002f)fprintf(stderr,"clip failure tick=%d travel=%g plane=%d d=%g xyz=%g,%g,%g\n",tick,s->motion.travel,p,distance,clipped[j].x,clipped[j].y,clipped[j].z);
                        assert(distance>-.0002f);
                    }
                }
            }
        }
        for(int i=0;i<3;i++)for(int j=0;j<3;j++) {
            float dot=0;for(int k=0;k<3;k++)dot+=view[k*4+i]*view[k*4+j];
            assert(fabsf(dot-(i==j?1:0))<1e-5f);
        }
        float eye[3];cave_world_point(s,x,y,s->motion.travel,eye);
        for(int i=0;i<3;i++)assert(fabsf(view[i]*eye[0]+view[4+i]*eye[1]+view[8+i]*eye[2]+view[12+i])<.0001f);
    }
    assert(s->motion.travel>500 && s->ready==CAVE_SLICES);
    /* Inactive controls do not change ordinary motion or view; activating
     * takes independent position/orientation, disabling restores normal view. */
    float normal_view[16],restored[16];cave_view(s,s->motion.travel,normal_view);
    unsigned rng=s->random;float travel=s->motion.travel;
    cave_flight_input(s,0,255,0,1,0);assert(!s->flight && s->flight_x==0);
    cave_view(s,s->motion.travel,restored);assert(!memcmp(normal_view,restored,sizeof(restored)));
    cave_flight_input(s,1,255,0,1,0);assert(s->flight && s->flight_throttle==1);
    assert(s->flight_axis_y>0);
    cave_options.invert_y=1;cave_flight_input(s,0,255,0,0,0);assert(s->flight_axis_y<0);
    cave_options.invert_y=0;
    s->flight_x=.2f;s->flight_y=.1f;cave_view(s,s->motion.travel,restored);
    assert(memcmp(normal_view,restored,sizeof(restored)));
    cave_flight_input(s,1,128,128,0,0);assert(!s->flight && s->flight_x==0 && s->flight_y==0);
    cave_view(s,s->motion.travel,restored);assert(!memcmp(normal_view,restored,sizeof(restored)));
    assert(s->random==rng && s->motion.travel==travel);
    CaveScene *speed_test=cave_create();assert(speed_test);
    memcpy(speed_test,s,sizeof(*s));
    cave_prepare(speed_test,bands,90,s->motion.previous+10000);
    float full_step=speed_test->motion.travel-s->motion.travel;assert(full_step>0);
    memcpy(speed_test,s,sizeof(*s));cave_options.speed=50;
    cave_prepare(speed_test,bands,90,s->motion.previous+10000);
    assert(fabsf((speed_test->motion.travel-s->motion.travel)-full_step*.5f)<.001f);
    cave_options.speed=100;cave_destroy(speed_test);
    /* Wide connected passage: steer beyond the former +/-1.2 offset, keep
     * position on stick release, clamp speed, and never travel backwards. */
    CaveScene *flight=cave_create();assert(flight);
    for(int i=0;i<CAVE_SHIP_VERTICES;i++) {
        const MdVertex *v=&cave_ship_mesh[i];
        assert(fabsf(v->x)*CAVE_SHIP_SCALE<=CAVE_SHIP_HALF_X-CAVE_SHIP_SKIN+1e-6f);
        assert(fabsf(v->y)*CAVE_SHIP_SCALE<=CAVE_SHIP_HALF_Y-CAVE_SHIP_SKIN+1e-6f);
        assert(fabsf(v->z)*CAVE_SHIP_SCALE<=CAVE_SHIP_HALF_Z-CAVE_SHIP_SKIN+1e-6f);
    }
    cave_options.flight_sensitivity=100;cave_options.flight_inertia=0;
    cave_flight_input(flight,1,128,128,0,0); /* also safe before cache warmup */
    unsigned long long clock=1000000;
    for(int i=0;i<16;i++)cave_prepare(flight,bands,90,clock+=1000);
    flight->motion.travel=0;flight->flight_x=flight->flight_y=0;
    flight->flight_roll=0;flight->flight_initialized=1;
    for(int i=0;i<CAVE_PATH_CACHE;i++)for(int j=0;j<CAVE_PATHS;j++) {
        flight->paths.frames[i].x[j]=flight->paths.frames[i].y[j]=.5f;
        flight->paths.frames[i].radius[j]=.7f;
    }
    /* This fixture replaces the field, so discard its old unrelated mesh. */
    for(int i=0;i<CAVE_SLICES;i++)flight->slices[i].count=0;
    cave_flight_input(flight,0,255,128,0,0);
    for(int i=0;i<40;i++) {
        cave_prepare(flight,bands,90,clock+=10000);
        for(int j=0;j<CAVE_SLICES;j++)flight->slices[j].count=0;
    }
    assert(flight->flight_x>1.2f && flight->motion.travel>0);
    /* With zero heading, XY is independent of the automatic path. */
    cave_flight_input(flight,0,128,128,0,0);flight->flight_yaw=0;
    float position=flight->flight_x;
    cave_prepare(flight,bands,90,clock+=10000);
    assert(flight->flight_x==position);
    cave_flight_input(flight,0,255,0,1,1);
    for(int i=0;i<250;i++) {
        float before=flight->motion.travel;
        cave_prepare(flight,bands,90,clock+=10000);
        assert(flight->motion.travel>=before);
        assert(fabsf(flight->flight_x)<5.7f && fabsf(flight->flight_y)<5.7f);
        cave_view(flight,flight->motion.travel,restored);
        for(int k=0;k<16;k++)assert(isfinite(restored[k]));
    }
    assert(flight->flight_speed==2);
    cave_flight_input(flight,0,0,255,-1,-1);
    for(int i=0;i<350;i++)cave_prepare(flight,bands,90,clock+=10000);
    assert(flight->flight_speed==.2f);
    cave_destroy(flight);
    /* Two disconnected forward passages: steering must not jump through
     * the intervening solid region, even at maximum player speed. */
    flight=cave_create();assert(flight);
    for(int i=0;i<16;i++)cave_prepare(flight,bands,90,clock+=1000);
    cave_flight_input(flight,1,255,128,0,0);
    flight->flight_x=flight->flight_y=flight->flight_roll=0;
    flight->flight_speed=2;
    for(int tick=0;tick<100;tick++) {
        for(int i=0;i<CAVE_PATH_CACHE;i++)for(int j=0;j<CAVE_PATHS;j++) {
            flight->paths.frames[i].x[j]=j<8?.5f:.75f;
            flight->paths.frames[i].y[j]=.5f;
            flight->paths.frames[i].radius[j]=.1f;
        }
        cave_prepare(flight,bands,90,clock+=10000);
        assert(flight->flight_x<1.5f);
        assert(cave_density(flight,flight->flight_x,flight->flight_y,flight->motion.travel)>.015f);
    }
    cave_destroy(flight);
    assert((cave_effect_color(0,0,0,1,1)>>24)==0x58);
    assert((cave_effect_color(0,0,1,1,1)>>24)==0x80);
    assert((cave_effect_color(0,0,0,1,0)>>24)==0xff);
    assert(clipped_visible>0);printf("Visible clipped triangles: %d\n",clipped_visible);
    printf("Cave: source oscillator fixture, 4000 path steps, 2100 gradient comparisons, 1024 cube cases, 1800 ticks; %d slabs, peak %d vertices/slab, allocation %zu bytes\n",s->built,peak,sizeof(*s));
    cave_destroy(s);
    return 0;
}
