/* SPDX-License-Identifier: GPL-2.0-or-later
 * Independent implementation of the field/surface approach observed in Monkey.
 * No executable code, random tables or assets are copied from the DLL.
 * See docs/MONKEY_GEOMETRY.md for verified observations vs PSP adaptations. */
#include "cave_visual.h"
#include "cave_topology.h"
#include "cave_style.h"
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static float mix(float a,float b,float t){return a+(b-a)*t;}
CaveOptions cave_options={1,1,1,1,1,8,8,-1,100,0};
static float flight_axis(int value) {
    float x=value-128;
    if(fabsf(x)<=20)return 0;
    return fmaxf(-1,fminf(1,(x-copysignf(20,x))/107));
}
void cave_flight_input(CaveScene *s,int toggle,int x,int y,int throttle) {
    if(!s)return;
    if(toggle){s->flight=!s->flight;s->flight_x=s->flight_y=0;}
    if(!s->flight)return;
    s->flight_axis_x=flight_axis(x);s->flight_axis_y=(cave_options.invert_y?1:-1)*flight_axis(y);
    s->flight_throttle=throttle>0?1:throttle<0?-1:0;
}
static void cave_flight_step(CaveScene *s,float dt) {
    if(!s->flight)return;
    float x,y;cave_camera(s,s->motion.travel,&x,&y);
    float roll=s->motion.roll-s->motion.bank,cr=cosf(roll),sr=sinf(roll);
    float ax=cr*s->flight_axis_x-sr*s->flight_axis_y,ay=sr*s->flight_axis_x+cr*s->flight_axis_y;
    float dx=fmaxf(-1.2f,fminf(1.2f,s->flight_x+ax*dt*1.7f));
    float dy=fmaxf(-1.2f,fminf(1.2f,s->flight_y+ay*dt*1.7f));
    /* A bounded field test, not triangle collision. Retract towards the
     * automatic safe path when the tunnel narrows; never change its RNG. */
    for(int i=0;i<8;i++) {
        if(cave_density(s,x+dx,y+dy,s->motion.travel)>.025f)break;
        dx*=.5f;dy*=.5f;
    }
    if(cave_density(s,x+dx,y+dy,s->motion.travel)<=.025f)dx=dy=0;
    s->flight_x=dx;s->flight_y=dy;
}
int cave_beat_response(CaveMotion *m,int amplitude,unsigned choice,float dt,int beat) {
    if(amplitude<0)amplitude=0;
    if(amplitude>16)amplitude=16;
    /* 0x10006904..0x10006a80. Do not smooth away a source beat impulse. */
    if(beat && amplitude) {
        choice%=100;
        if(choice<9)return 1; /* render-style change, not a new tunnel */
        if(choice<=60) {m->direction=m->direction>0?-1:1;m->spin=amplitude*.007f*m->direction;}
        else m->forward=amplitude*.25f;
    } else {
        float adjustment=amplitude>8?(amplitude-8)*.00025f:
            -.03f*powf((8-amplitude)*.125f,2.5f);
        m->spin*=powf(.989f+adjustment,30*dt);
        m->forward*=powf(.988f+adjustment,30*dt);
    }
    m->roll=remainderf(m->roll+m->spin*30*dt,6.283185307f);
    return 0;
}
/* Original procedural texture retained, not a Monkey asset. */
void cave_texture(uint32_t *pixels) {
    for(int y=0;y<CAVE_TEXTURE;y++)for(int x=0;x<CAVE_TEXTURE;x++) {
        float a=x*(6.283185307f/CAVE_TEXTURE),b=y*(6.283185307f/CAVE_TEXTURE);
        float grain=sinf(a*7+b*3)*cosf(b*5-a*2);
        int shade=(int)(138+35*sinf(a*2+1.4f*sinf(b*3))+24*cosf(b*4-a)+16*grain);
        pixels[y*CAVE_TEXTURE+x]=0xff000000U|(unsigned)shade|((unsigned)(shade*9/10)<<8)|((unsigned)(shade*3/4)<<16);
    }
}
/* 0x100016cf..0x10001721: row-major two-angle octave rotation. */
void cave_noise_rotation(float out[9],float a,float b) {
    float ca=cosf(a),sa=sinf(a),cb=cosf(b),sb=sinf(b);
    float m[9]={ca*cb,-sa*cb,-sb,sa,ca,0,ca*sb,-sa*sb,cb};
    memcpy(out,m,sizeof(m));
}
static float noise3(const CaveScene *s,float x,float y,float z,float gradient[3]) {
    float fx=floorf(x),fy=floorf(y),fz=floorf(z);
    int ix=(int)fx&15,iy=(int)fy&15,iz=(int)fz&15;
    x-=fx;y-=fy;z-=fz;
    float dx=6*x*(1-x),dy=6*y*(1-y),dz=6*z*(1-z);
    x=x*x*(3-2*x);y=y*y*(3-2*y);z=z*z*(3-2*z);
    float row[4],gx[4];
    for(int k=0;k<2;k++)for(int j=0;j<2;j++) {
        int base=(((iz+k)&15)*16+((iy+j)&15))*16;
        float a=s->noise[base+ix],b=s->noise[base+((ix+1)&15)];
        row[k*2+j]=mix(a,b,x);gx[k*2+j]=(b-a)*dx;
    }
    gradient[0]=mix(mix(gx[0],gx[1],y),mix(gx[2],gx[3],y),z);
    gradient[1]=mix(row[1]-row[0],row[3]-row[2],z)*dy;
    gradient[2]=(mix(row[2],row[3],y)-mix(row[0],row[1],y))*dz;
    return mix(mix(row[0],row[1],y),mix(row[2],row[3],y),z);
}
void cave_camera(const CaveScene *s,float z,float *x,float *y) {
    CavePathFrame p;
    if(cave_paths_sample(&s->paths,z,&p,NULL)) {*x=(p.x[0]-.5f)*12;*y=(p.y[0]-.5f)*12;}
    else {*x=0;*y=0;}
    /* Source 0x10007486..0x10007596, normalized coordinates mapped by 6.
     * Keep the original three-sine eye sway; reject excursions into a wall. */
    float sx=6*(.0227f*sinf(z*.0139f+41)+.0216f*sinf(z*.0197f+98)+.023f*sinf(z*.0179f+28));
    float sy=6*(.0226f*sinf(z*.0173f+73)+.0207f*sinf(z*.0152f+23)+.024f*sinf(z*.0129f+11));
    float gain=1;
    for(int i=0;i<4;i++) {
        if(cave_density(s,*x+gain*sx,*y+gain*sy,z)>.015f){*x+=gain*sx;*y+=gain*sy;break;}
        gain*=.5f;
    }
}
void cave_world_point(const CaveScene *s,float x,float y,float z,float out[3]) {
    int index=(int)floorf(z);float fraction=z-index;
    if(index<0){out[0]=x;out[1]=y;out[2]=-z;return;}
    const CavePose *a=&s->poses[index%CAVE_PATH_CACHE],*b=&s->poses[(index+1)%CAVE_PATH_CACHE];
    /* At an exact profile boundary only that profile is needed. In
     * particular the upper face of the newest slab already has its pose,
     * while the following pose has deliberately not been generated yet.
     * Falling back to straight geometry here tears EVERY curved slab. */
    if(a->index!=index || (fraction!=0 && b->index!=index+1)){out[0]=x;out[1]=y;out[2]=-z;return;}
    /* Original vertex loft: linear blend of the two rotated cross sections;
     * the section axis is translated by one profile between their origins. */
    for(int k=0;k<3;k++) {
        float p=a->center[k]+a->rotation[k*3]*x/6+a->rotation[k*3+1]*y/6;
        if(fraction!=0) {
            float q=b->center[k]+b->rotation[k*3]*x/6+b->rotation[k*3+1]*y/6;
            p=mix(p,q,fraction);
        }
        out[k]=p*(k<2?6:1);
    }
}
void cave_view(const CaveScene *s,float z,float matrix[16]) {
    float x,y;cave_camera(s,z,&x,&y);
    CavePathFrame ahead;
    float dx=0,dy=0;
    /* Recovered six-profile look-ahead and source target sway. */
    if(cave_paths_sample(&s->paths,z+6,&ahead,NULL)) {
        dx=(ahead.x[0]-.5f)*12-x;dy=(ahead.y[0]-.5f)*12-y;
    }
    float seed=s->paths.seed;
    /* 0x10007300..0x1000747e: .4 normalized sway, XY world scale=6.
     * Fixed movement/FOV profile keeps the source gain gates at unity. */
    dx+=2.4f*(.21f*sinf(seed*1.2f+z*.0119f+11)+.16f*sinf(seed*3.1f+z*.0137f+46)+.19f*sinf(seed*1.4f+z*.0059f+38));
    dy+=2.4f*(.12f*sinf(seed*2.4f+z*.0103f+83)+.17f*sinf(seed*2.7f+z*.0122f+29)+.19f*sinf(seed*1.7f+z*.0069f+91));
    if(s->flight){x+=s->flight_x;y+=s->flight_y;}
    float eye[3],target[3],up_point[3];
    cave_world_point(s,x,y,z,eye);cave_world_point(s,x+dx,y+dy,z+6,target);
    cave_world_point(s,x,y+1,z,up_point);
    float forward[3],up[3],right[3],length=0;
    for(int k=0;k<3;k++){forward[k]=target[k]-eye[k];up[k]=up_point[k]-eye[k];length+=forward[k]*forward[k];}
    float inverse=1/sqrtf(length);
    for(int k=0;k<3;k++)forward[k]*=inverse;
    for(int k=0;k<3;k++)right[k]=forward[(k+1)%3]*up[(k+2)%3]-forward[(k+2)%3]*up[(k+1)%3];
    inverse=1/sqrtf(right[0]*right[0]+right[1]*right[1]+right[2]*right[2]);
    for(int k=0;k<3;k++)right[k]*=inverse;
    for(int k=0;k<3;k++)up[k]=right[(k+1)%3]*forward[(k+2)%3]-right[(k+2)%3]*forward[(k+1)%3];
    float view[16]={0};view[15]=1;
    for(int k=0;k<3;k++) {
        view[k*4]=right[k];view[k*4+1]=up[k];view[k*4+2]=-forward[k];
        view[12]-=right[k]*eye[k];view[13]-=up[k]*eye[k];view[14]+=forward[k]*eye[k];
    }
    float roll=s->motion.roll-s->motion.bank;
    float cr=cosf(roll),sr=sinf(roll);
    for(int k=0;k<4;k++) {
        float right=view[k*4],up=view[k*4+1];
        view[k*4]=cr*right+sr*up;view[k*4+1]=cr*up-sr*right;
    }
    memcpy(matrix,view,sizeof(view));
}
typedef struct {float x[CAVE_PATHS],y[CAVE_PATHS],dx[CAVE_PATHS],dy[CAVE_PATHS],inverse[CAVE_PATHS],dr[CAVE_PATHS];} CaveField;
static int prepare_field(const CaveScene *s,CaveField *p,float z) {
    CavePathFrame frame,derivative;
    if(!cave_paths_sample(&s->paths,z,&frame,&derivative))return 0;
    for(int i=0;i<CAVE_PATHS;i++) {
        p->x[i]=(frame.x[i]-.5f)*12;p->y[i]=(frame.y[i]-.5f)*12;
        p->dx[i]=derivative.x[i]*12;p->dy[i]=derivative.y[i]*12;
        float radius=frame.radius[i]*12;
        p->inverse[i]=1/(radius*radius);p->dr[i]=derivative.radius[i]/frame.radius[i];
    }
    return 1;
}
static float sample_field(const CaveScene *s,const CaveField *p,float x,float y,float z,float gradient[3]) {
    float field=0;gradient[0]=gradient[1]=gradient[2]=0;
    /* Compact radial contribution seen at 0x10001f62: q*q-q+0.25,
     * q=(dx*dx+dy*dy)/radius^2, contributing only for q<0.5. */
    for(int i=0;i<CAVE_PATHS;i++) {
        float dx=x-p->x[i],dy=y-p->y[i],q=(dx*dx+dy*dy)*p->inverse[i];
        if(q<.5f) {
            field+=(q-.5f)*(q-.5f);
            float factor=4*(q-.5f)*p->inverse[i];
            gradient[0]+=factor*dx;gradient[1]+=factor*dy;
            gradient[2]-=factor*(dx*p->dx[i]+dy*p->dy[i]);
            gradient[2]-=4*(q-.5f)*q*p->dr[i];
        }
    }
    float noise=0,amplitude=1,frequency=.35f;
    float ng[3]={0};
    for(int octave=0;octave<3;octave++) {
        const float *m=s->noise_matrix[octave],*offset=s->noise_offset[octave];
        float v[3],g[3];
        for(int j=0;j<3;j++)v[j]=(m[j*3]*x+m[j*3+1]*y+m[j*3+2]*z)*frequency+offset[j];
        noise+=amplitude*noise3(s,v[0],v[1],v[2],g);
        for(int j=0;j<3;j++)ng[j]+=amplitude*frequency*(m[j]*g[0]+m[3+j]*g[1]+m[6+j]*g[2]);
        amplitude*=.51626223f;frequency*=1.937f;
    }
    const float iso=.08f;
    if(noise>-1 && noise<1)for(int j=0;j<3;j++)gradient[j]+=iso*.29f*ng[j];
    if(noise<-1)noise=-1;
    if(noise>1)noise=1;
    return field+iso*(-.22f+(noise+1)*.5f*(.36f+.22f))-iso;
}
float cave_sample(const CaveScene *s,float x,float y,float z,float gradient[3]) {
    CaveField p;
    if(!prepare_field(s,&p,z)){gradient[0]=gradient[1]=gradient[2]=0;return 0;}
    return sample_field(s,&p,x,y,z,gradient);
}
float cave_density(const CaveScene *s,float x,float y,float z) {
    float gradient[3];return cave_sample(s,x,y,z,gradient);
}
/* 0x10002300..0x10002708: material weights are the radial kernel, not
 * the noise and not interpolated corner colors. Evaluate at the edge vertex.
 * Coordinates here are the source's normalized cross section. */
void cave_material_sample(const CaveScene *s,int profile,float x,float y,float fraction,float rgba[4],float normal[3]) {
    memset(rgba,0,4*sizeof(float));memset(normal,0,3*sizeof(float));
    const CavePathFrame *a=&s->paths.frames[profile%CAVE_PATH_CACHE],*b=&s->paths.frames[(profile+1)%CAVE_PATH_CACHE];
    if(a->index!=profile || b->index!=profile+1)return;
    float sum=0;
    for(int i=0;i<CAVE_PATHS;i++) {
        float cx=mix(a->x[i],b->x[i],fraction),cy=mix(a->y[i],b->y[i],fraction);
        float radius=mix(a->radius[i],b->radius[i],fraction);
        float dx=x-cx,dy=y-cy,q=(dx*dx+dy*dy)/(radius*radius);
        if(q>=.5f)continue;
        float weight=q*q-q+.25f;sum+=weight;
        for(int k=0;k<4;k++)rgba[k]+=weight*s->material[i][k];
        /* Preserve the source normal law: no radius derivative, no noise
         * derivative and no inverse-radius factor on the XY terms. */
        normal[0]+=(4*q-2)*dx;normal[1]+=(4*q-2)*dy;
        normal[2]+=2*(1-2*q*q)*(dy*(b->y[i]-a->y[i])+dx*(b->x[i]-a->x[i]));
    }
    if(sum>1e-12f)for(int k=0;k<4;k++)rgba[k]/=sum;
    normal[2]*=4;
    float length=normal[0]*normal[0]+normal[1]*normal[1]+normal[2]*normal[2];
    if(length>1e-11f){float inv=-1/sqrtf(length);for(int k=0;k<3;k++)normal[k]*=inv;}
    else {normal[0]=1;normal[1]=normal[2]=0;}
}
/* Classic 256-case MC as in the original. Max five triangles per cube,
 * including the original sign-only choices on ambiguous faces. */
static int polygonize(const float f[8],const float gradients[8][3],const CaveLight light[2],MdVertex *out,float (*normal_out)[3],unsigned short *edges,int capacity,float x,float y,float z) {
    static const unsigned char corners[8][3]={{0,0,0},{1,0,0},{1,1,0},{0,1,0},{0,0,1},{1,0,1},{1,1,1},{0,1,1}};
    static const unsigned char ends[12][2]={{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
    unsigned mask=0;for(int i=0;i<8;i++)if(f[i]<=0)mask|=1U<<i;
    const signed char *topology=cave_topology[mask];
    int required=0;while(required<15 && topology[required]>=0)required++;
    if(capacity<required)return -1;
    MdVertex points[12];float normals[12][3];
    for(int e=0;e<12;e++) {
        int a=ends[e][0],b=ends[e][1];
        if((f[a]>0)==(f[b]>0))continue;
        float t=f[a]/(f[a]-f[b]);
        points[e]=(MdVertex){0,0,0xffffffff,
            x+mix(corners[a][0],corners[b][0],t),
            y+mix(corners[a][1],corners[b][1],t),
            z+mix(corners[a][2],corners[b][2],t)};
        if(gradients)points[e].color=cave_shade(light,light+1,points[e].z-z,mix(gradients[a][0],gradients[b][0],t),
            mix(gradients[a][1],gradients[b][1],t),mix(gradients[a][2],gradients[b][2],t));
        if(normal_out)for(int k=0;k<3;k++)normals[e][k]=mix(gradients[a][k],gradients[b][k],t);
    }
    int count=0;
    for(int k=0;k<required;k+=3) {
            MdVertex a=points[(int)topology[k]],b=points[(int)topology[k+1]],c=points[(int)topology[k+2]];
            float ux=b.x-a.x,uy=b.y-a.y,uz=b.z-a.z,vx=c.x-a.x,vy=c.y-a.y,vz=c.z-a.z;
            float nx=uy*vz-uz*vy,ny=uz*vx-ux*vz,nz=ux*vy-uy*vx;
            float norm=sqrtf(nx*nx+ny*ny+nz*nz);
            if(norm<.000001f)continue;
            unsigned color=0xffffffff;
            MdVertex triangle[3]={a,b,c};
            for(int j=0;j<3;j++) {
                MdVertex p=triangle[j];if(!gradients)p.color=color;
                /* 0x10005afc..0x10005b13: base UV = normalized X plus
                 * longitudinal 1/96 offset, normalized Y. Reverse depth to
                 * match our forward-growing slabs. Do not modulo individual
                 * vertices: GE repeat wraps after interpolation, without seams. */
                p.u=.5f+p.x/12-p.z/96;p.v=.5f+p.y/12;
                if(edges) {
                    int e=topology[k+j],a=ends[e][0],b=ends[e][1],axis=0;
                    while(corners[a][axis]==corners[b][axis])axis++;
                    int ix=(int)(x+CAVE_GRID*.5f)+(corners[a][0]<corners[b][0]?corners[a][0]:corners[b][0]);
                    int iy=(int)(y+CAVE_GRID*.5f)+(corners[a][1]<corners[b][1]?corners[a][1]:corners[b][1]);
                    int iz=corners[a][2]<corners[b][2]?corners[a][2]:corners[b][2];
                    edges[count]=(axis*2+iz)*(CAVE_GRID+1)*(CAVE_GRID+1)+iy*(CAVE_GRID+1)+ix;
                }
                p.z=-p.z;if(normal_out)memcpy(normal_out[count],normals[(int)topology[k+j]],3*sizeof(float));out[count++]=p;
            }
    }
    return count;
}
int cave_polygonize(const float f[8],MdVertex *out,int capacity,float x,float y,float z) {
    return polygonize(f,NULL,NULL,out,NULL,NULL,capacity,x,y,z);
}
static unsigned random_step(unsigned *state) {
    unsigned n=*state;n^=n<<13;n^=n>>17;n^=n<<5;return *state=n;
}

CaveScene *cave_create(void) {
    CaveScene *s=memalign(64,sizeof(*s));if(!s)return NULL;
    memset(s,0,sizeof(*s));
    s->planes[0].z=s->planes[1].z=-1;
    for(int i=0;i<CAVE_SLICES;i++)s->slices[i].index=-1;
    unsigned random=0x45319a7;
    for(int i=0;i<4096;i++) {
        s->noise[i]=2*(random_step(&random)%731)/730.f-1;
    }
    for(int i=0;i<3;i++) {
        for(int j=0;j<3;j++)s->noise_offset[i][j]=(random_step(&random)%731)*(16.f/730);
        float a=(random_step(&random)%731)*(6.28f/730);
        float b=(random_step(&random)%731)*(6.28f/730);
        cave_noise_rotation(s->noise_matrix[i],a,b);
    }
    cave_paths_init(&s->paths,0x7149823);
    s->random=0x7291ba5;s->motion.direction=1;
    s->texture_style=1;
    for(int i=0;i<CAVE_PATH_CACHE;i++)s->poses[i].index=-1;
    s->poses[0]=(CavePose){.rotation={1,0,0,0,1,0,0,0,1},.index=0};s->pose_next=1;
    s->texture_transition[0]=s->texture_transition[1]=-1;
    for(int i=0;i<3;i++)s->material_phase[i]=(random_step(&s->random)%917)*.68558955f;
    memcpy(s->background_phase,s->material_phase,sizeof(s->background_phase));
    cave_background_update(s->background_rgb,s->paths.seed,s->background_phase);
    for(int i=0;i<CAVE_PATHS;i++) {
        s->material[i][3]=random_step(&s->random)&1;
        do {
            for(int k=0;k<3;k++)s->material[i][k]=(random_step(&s->random)%471)*.5425531864f;
        } while(s->material[i][0]+s->material[i][1]+s->material[i][2]<1.3f);
    }
    for(int i=0;i<2048;i++)s->random_values[i]=(random_step(&s->random)%1573)*.00063572789f;
    /* One extra future profile supplies the derivative of the upper plane. */
    for(int i=0;i<3;i++)cave_paths_step(&s->paths);
    return s;
}
void cave_destroy(CaveScene *s){free(s);}
static void cave_rebase(CaveScene *s) {
    const CavePose *p=&s->poses[(int)s->motion.travel%CAVE_PATH_CACHE];
    if(p->index!=(int)s->motion.travel)return;
    float shift[3]={p->center[0]*6,p->center[1]*6,p->center[2]};
    if(fabsf(shift[0])<256 && fabsf(shift[1])<256 && fabsf(shift[2])<256)return;
    /* Keep GE and clipping arithmetic close to the origin on long journeys.
     * Translate every cached representation together; no RNG/state reset. */
    for(int i=0;i<CAVE_PATH_CACHE;i++)for(int k=0;k<3;k++)s->poses[i].center[k]-=shift[k]/(k<2?6:1);
    for(int i=0;i<CAVE_SLICES;i++) {
        CaveSlice *slice=&s->slices[i];
        MdVertex *streams[4]={slice->vertices,slice->secondary,slice->wire,slice->hair};
        for(int j=0;j<4;j++)for(int k=0;k<(j==3?slice->hair_count:slice->count);k++) {
            MdVertex *v=streams[j]+k;v->x-=shift[0];v->y-=shift[1];v->z-=shift[2];
        }
    }
}
CaveSlice *cave_prepare(CaveScene *s,const unsigned char bands[12],int level,unsigned long long now) {
    (void)level;
    float dt=s->motion.previous && now>=s->motion.previous?(now-s->motion.previous)*.000001f:0;
    s->motion.previous=now;if(dt>.1f)dt=.1f;
    float bass=(bands[0]+bands[1]+bands[2])/300.f;if(bass>1)bass=1;
    float attack=fmaxf(0,bass-s->motion.bass);
    /* Detection uses PSP PCM bands, not Winamp's analysis callback. The
     * response itself uses the recovered original impulse/decay law. */
    int beat=cave_options.beat && bass>.08f && attack>(.22f-.01f*cave_options.sensitivity) &&
        now-s->motion.last_beat>250000ULL;
    if(beat)s->motion.last_beat=now;
    int change=cave_beat_response(&s->motion,cave_options.amplitude,beat?random_step(&s->random):0,dt,beat);
    if((change && cave_options.style<0) || (cave_options.style>=0 && s->style!=cave_options.style)) {
        int next=cave_options.style;
        if(next<0)do{next=random_step(&s->random)%9;}while(next==s->style);
        s->style=next;
        s->normal_effect=next==4||next==5||(next==8&&random_step(&s->random)%3==0);
        s->texture_style=next<2||next==8||(next==7&&(random_step(&s->random)&1));
        s->black=(next==2||next==3)?random_step(&s->random)%5==0:
            next==6?random_step(&s->random)%10==0:next==7?(int)(random_step(&s->random)&1):0;
    }
    s->motion.pulse+=(attack-s->motion.pulse)*(1-expf(-dt*4));
    s->motion.phase+=dt*.07f;
    if(s->motion.phase>=6.283185307f)s->motion.phase-=6.283185307f;
    s->motion.bass+=(bass-s->motion.bass)*(1-expf(-dt*2));
    float step=cave_forward_step(dt,s->motion.forward)*(cave_options.speed*.01f);
    if(s->flight)step*=s->flight_throttle>0?1.5f:s->flight_throttle<0?.5f:1;
    if(s->ready>=12) {
        float proposed=s->motion.travel+step;
        /* Never travel into an unprepared slab; audio is never stalled. */
        if(proposed<s->next-6)s->motion.travel=proposed;
    }
    CavePathFrame bank_near,bank_far,bank_next,bank_next_far;
    int profile=(int)floorf(s->motion.travel);float fraction=s->motion.travel-profile;
    if(cave_paths_sample(&s->paths,profile+4,&bank_near,NULL) &&
       cave_paths_sample(&s->paths,profile+9,&bank_far,NULL) &&
       cave_paths_sample(&s->paths,profile+6,&bank_next,NULL) &&
       cave_paths_sample(&s->paths,profile+11,&bank_next_far,NULL)) {
        float difference=mix(bank_near.x[0]-bank_far.x[0],bank_next.x[0]-bank_next_far.x[0],fraction);
        const CavePose *p=&s->poses[(profile+4)%CAVE_PATH_CACHE],*q=&s->poses[(profile+5)%CAVE_PATH_CACHE];
        float curvature=p->index==profile+4&&q->index==profile+5?mix(p->bank_curve,q->bank_curve,fraction):0;
        float target=atanf(15*(difference-6.5f*curvature));
        /* Source retention contains BOTH distance and frame-rate powers. */
        s->motion.bank+=(target-s->motion.bank)*(1-powf(.43f+.5f*powf(.86f,step),14*dt));
    }
    cave_rebase(s);
    cave_flight_step(s,dt);
    int first=(int)floorf(s->motion.travel);
    /* Keep the original forward horizon AND three rear slabs. A banked view
     * can still see those at its edges; do not recycle them at camera Z. */
    if(s->next>=first+CAVE_AHEAD)return NULL;
    int index=s->next;
    while(s->pose_next<=index+1) {
        float rotation[9];cave_bend_rotation(&s->bend,s->paths.seed,s->pose_next-1,rotation);
        cave_pose_next(&s->poses[(s->pose_next-1)%CAVE_PATH_CACHE],rotation,&s->poses[s->pose_next%CAVE_PATH_CACHE]);
        s->pose_next++;
    }
    while(s->paths.next<=index+2)cave_paths_step(&s->paths);
    CaveSlice *slice=&s->slices[index%CAVE_SLICES];
    float (*planes[2])[CAVE_GRID+1];
    float (*normals[2])[CAVE_GRID+1][3];
    for(int k=0;k<2;k++) {
        int z=index+k;CavePlane *plane=&s->planes[z&1];
        planes[k]=plane->field;normals[k]=plane->gradient;
        if(plane->z!=z) {
            CaveField p;if(!prepare_field(s,&p,z))return NULL;
            for(int y=0;y<=CAVE_GRID;y++)for(int x=0;x<=CAVE_GRID;x++)
                planes[k][y][x]=sample_field(s,&p,x-CAVE_GRID*.5f,y-CAVE_GRID*.5f,z,normals[k][y][x]);
            plane->z=z;s->sampled_planes++;
        }
    }
    int used=0;
    CaveLight light[2];cave_lighting(light,s->paths.seed,index);cave_lighting(light+1,s->paths.seed,index+1);
    /* 0x10005392..0x100053bf: the extra ambient term is guarded by
     * 0x129a4, confirmed by its INI key "multitex", NOT the black flag. */
    if(cave_options.multitexture){light[0].ambient+=.07f;light[1].ambient+=.07f;}
    for(int y=0;y<CAVE_GRID;y++)for(int x=0;x<CAVE_GRID;x++) {
        float f[8]={planes[0][y][x],planes[0][y][x+1],planes[0][y+1][x+1],planes[0][y+1][x],
                    planes[1][y][x],planes[1][y][x+1],planes[1][y+1][x+1],planes[1][y+1][x]};
        float gradients[8][3];
        static const unsigned char corner[8][3]={{0,0,0},{1,0,0},{1,1,0},{0,1,0},{0,0,1},{1,0,1},{1,1,1},{0,1,1}};
        for(int i=0;i<8;i++)memcpy(gradients[i],normals[corner[i][2]][y+corner[i][1]][x+corner[i][0]],sizeof(gradients[i]));
        int n=polygonize(f,gradients,light,slice->vertices+used,s->normals+used,s->edge_id+used,CAVE_MAX_VERTICES-used,x-CAVE_GRID*.5f,y-CAVE_GRID*.5f,index);
        if(n<0){slice->count=0;return NULL;} /* Mathematically unreachable capacity guard. */
        used+=n;
    }
    slice->hair_count=0;
    /* Source texture banks: five A, two B images; replace a bank only while
     * its weight is faded out. Progress is in generated world profiles. */
    if(!cave_options.multitexture) {
        if(random_step(&s->random)%400==0)s->texture_index[0]=random_step(&s->random)%5;
    } else {
        for(int k=0;k<2;k++) {
            if(s->texture_transition[0]<0 && s->texture_transition[1]<0 && random_step(&s->random)%650==0)
                s->texture_transition[k]=.01f;
            float p=s->texture_transition[k];
            if(p>=34 && p<35)s->texture_index[k]=random_step(&s->random)%(k?2:5);
            if(p>=0)s->texture_transition[k]=p>=68?-1:p+1;
        }
    }
    /* Exact grid-edge identity: shared vertices contribute once to material
     * effects and the mean normal, without lossy position hashing. */
    short first_vertex[CAVE_EDGE_SLOTS];for(int i=0;i<CAVE_EDGE_SLOTS;i++)first_vertex[i]=-1;
    float average=0;int unique=0;
    for(int i=0;i<used;i++) {
        if(first_vertex[s->edge_id[i]]>=0)continue;
        first_vertex[s->edge_id[i]]=i;unique++;
        float *n=s->normals[i];MdVertex *v=slice->vertices+i;
        cave_material_sample(s,index,.5f+v->x/12,.5f+v->y/12,-v->z-index,s->rgba[i],n);
        average+=n[2];
    }
    if(unique)average/=unique;
    float hair_length=powf(2,.8f*sinf(.629f*s->paths.seed+.0137f*index)-.4f)*.045f;
    if(cave_options.transparent_hair)hair_length*=1.3f;
    if(s->black)hair_length*=1.3f;
    float seed=s->paths.seed;
    float frequency_scale=1.3f+.2f*(sinf(seed*.91f+index*.00433f+5)+sinf(seed*.72f+index*.00283f+2))
        +.3f*sinf(seed*.53f+index*.00391f+3);
    float frequency[3]={2.4f*frequency_scale*(.067f+.0627f*sinf(seed*.27f+index*.0036f+1)),
        2.4f*frequency_scale*(.047f+.0444f*sinf(seed*.17f+index*.0042f+4)),
        2.4f*frequency_scale*(.038f+.0345f*sinf(seed*.33f+index*.0057f+5))};
    float normal_threshold=.0085f+.4f*powf(.5f+.22f*sinf(seed*.16f+index*.00472f)+.27f*sinf(seed*.33f+index*.00312f),15);
    for(int i=0;i<used;i++) {
        MdVertex *v=slice->vertices+i;float *n=s->normals[i];
        if(first_vertex[s->edge_id[i]]!=i){v->color=slice->vertices[first_vertex[s->edge_id[i]]].color;continue;}
        float x=v->x/6,y=v->y/6,z=-v->z-index;
        const float *rgba=s->rgba[i];float normal[3]={n[0],n[1],n[2]};
        if(s->normal_effect && !s->black && s->random_values[(++s->random_cursor)&2047]>normal_threshold)
            for(int k=0;k<3;k++)normal[k]=-normal[k];
        /* Recovered spatial alpha envelope (0x100053c3/0x10005918). */
        float t=-v->z;
        /* Spatial RGB waves and brightness lift from 0x10005663. */
        float waves[3]={157+60*sinf(s->material_phase[0]+z*frequency[0]+.21f*y+.131f*x),
            137+55*sinf(s->material_phase[1]+z*frequency[1]-.377f*x+.021f*y),
            117+50*sinf(s->material_phase[2]+z*frequency[2]-.519f*x-.313f*y)};
        float material[3];
        for(int k=0;k<3;k++) {
            float value=(waves[k]+rgba[k])*.5f;
            material[k]=(cave_options.multitexture?value*.39f+155.55f:value*.44f+142.8f)/255;
        }
        v->color=cave_shade_material(light,light+1,z,normal[0],normal[1],normal[2],material);
        float alpha=.1379f*((1-y)*sinf(seed*.231f+index*.00272f)+y*sinf(seed*.231f+index*.00272f+1.13f))
            +.1034f*((1-x)*sinf(seed*.111f+index*.00317f)+x*sinf(seed*.111f+index*.00317f+1.61f))
            +.2759f*sinf(seed*.371f+t*.0738f)+.4827f*sinf(seed*.171f+t*.0232f);
        alpha=cave_unit(((alpha*290+rgba[3]*255-128)*.57f+128)/255);
        for(int k=0;k<2;k++)if(s->texture_transition[k]>=0) {
            float fade=(s->texture_transition[k]+z)/34;
            if(fade>1.05f)fade=2.1f-fade;
            fade=cave_unit(fade);alpha=k?alpha*(1-fade):alpha*(1-fade)+fade;
        }
        unsigned a=(unsigned)(255*alpha);
        v->color=(v->color&0xffffff)|(a<<24);
        s->random_cursor+=5;if(s->random_cursor>=2043)s->random_cursor=0;
        const float *random=s->random_values+s->random_cursor;
        float gate=n[2]-average-.12f+random[0]*.2f;
        if(gate<=0 || slice->hair_count+2>CAVE_HAIR_VERTICES)continue;
        float distance=hair_length*fminf(1,gate*3)*(.6f+.8f*random[1]);
        MdVertex from=*v,to=*v;
        from.color=to.color=0xffffffff;
        to.x-=(n[0]+(random[2]-.5f)*.25f)*distance*6;
        to.y-=(n[1]+(random[3]-.5f)*.25f)*distance*6;
        to.z+=(n[2]+(random[4]-.5f)*.25f)*distance;
        slice->hair[slice->hair_count++]=from;slice->hair[slice->hair_count++]=to;
    }
    cave_background_update(s->background_rgb,seed,s->background_phase);
    for(int k=0;k<3;k++) {
        s->material_phase[k]=remainderf(s->material_phase[k]+frequency[k],6.283185307f);
        /* Background uses .013 times the accumulated material phase; using
         * the already 2*pi-wrapped material phase destroys its slow cycle. */
        s->background_phase[k]=remainderf(s->background_phase[k]+frequency[k],6.283185307f/.013f);
    }
    for(int i=0;i<used;i++) {
        MdVertex *a=slice->vertices+i,*b=slice->secondary+i;float world[3];
        /* Separate displaced wire geometry; don't alter surface/depth/UVs.
         * Reuse the canonical edge normal for duplicate triangle vertices. */
        const float *n=s->normals[first_vertex[s->edge_id[i]]];
        float offset=.002f;
        if(s->style==3 || s->style==5) {
            float envelope=.5f+.2f*sinf(.63f*seed+.083f*index)+.3f*sinf(.1f*seed+.061f*index);
            offset*=powf(2,1.2f*(2*cave_path_shape(envelope,1)-1));
            offset*=n[2]-average-.21f<0?-3:10;
        }
        MdVertex *wire=slice->wire+i;*wire=*a;
        cave_world_point(s,a->x-n[0]*offset*6,a->y-n[1]*offset*6,-a->z,world);
        const CavePose *pose=&s->poses[index%CAVE_PATH_CACHE];
        for(int k=0;k<3;k++)world[k]+=pose->rotation[k*3+2]*n[2]*offset*2*(k<2?6:1);
        wire->x=world[0];wire->y=world[1];wire->z=world[2];
        *b=*a;b->u=.5f+a->x/12;
        cave_world_point(s,a->x,a->y,-a->z,world);
        a->x=b->x=world[0];a->y=b->y=world[1];a->z=b->z=world[2];
    }
    for(int i=0;i<slice->hair_count;i+=2) {
        MdVertex *v=slice->hair+i;float world[3],delta[3]={v[1].x-v[0].x,v[1].y-v[0].y,v[1].z-v[0].z};
        /* A tip may protrude into an unbuilt next profile. The hair direction
         * is local to its root's profile, so transform it with that basis. */
        const CavePose *pose=&s->poses[index%CAVE_PATH_CACHE];
        cave_world_point(s,v->x,v->y,-v->z,world);
        v[0].x=world[0];v[0].y=world[1];v[0].z=world[2];
        for(int k=0;k<3;k++)world[k]+=(pose->rotation[k*3]*delta[0]/6+
            pose->rotation[k*3+1]*delta[1]/6+pose->rotation[k*3+2]*delta[2])*(k<2?6:1);
        v[1].x=world[0];v[1].y=world[1];v[1].z=world[2];
    }
    slice->texture_a=s->texture_index[0];slice->texture_b=s->texture_index[1];
    slice->index=index;slice->count=used;s->next++;s->built++;
    if(s->ready<CAVE_SLICES)s->ready++;
    return slice;
}
