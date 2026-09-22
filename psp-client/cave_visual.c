/* SPDX-License-Identifier: GPL-2.0-or-later
 * Independent implementation of the field/surface approach observed in Monkey.
 * No executable code, random tables or assets are copied from the DLL.
 * See docs/MONKEY_GEOMETRY.md for verified observations vs PSP adaptations. */
#include "cave_visual.h"
#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static float mix(float a,float b,float t){return a+(b-a)*t;}
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
static unsigned cave_light(float nx,float ny,float nz) {
    float norm=sqrtf(nx*nx+ny*ny+nz*nz);
    float light=.25f;
    if(norm>.000001f)light+=.70f*fabsf((nx*.4f+ny*.6f+nz*.69282f)/norm);
    if(light>1)light=1;
    return 0xff000000U|(unsigned)(light*225)|((unsigned)(light*185)<<8)|((unsigned)(light*140)<<16);
}

/* Marching Cubes via face-edge connectivity, not a copied 256-case table.
 * Ambiguous faces use the same center-sign rule on either adjacent cell.
 * At most 12 intersections => 10 triangles. Positive field is cave interior. */
static int polygonize(const float f[8],const float gradients[8][3],MdVertex *out,int capacity,float x,float y,float z) {
    static const unsigned char corners[8][3]={{0,0,0},{1,0,0},{1,1,0},{0,1,0},{0,0,1},{1,0,1},{1,1,1},{0,1,1}};
    static const unsigned char ends[12][2]={{0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},{0,4},{1,5},{2,6},{3,7}};
    static const unsigned char faces[6][4]={{0,1,2,3},{4,5,6,7},{0,9,4,8},{1,10,5,9},{2,11,6,10},{3,8,7,11}};
    static const unsigned char face_corners[6][4]={{0,1,2,3},{4,5,6,7},{0,1,5,4},{1,2,6,5},{2,3,7,6},{3,0,4,7}};
    if(capacity<30)return -1;
    int links[12][2],degree[12]={0},active[12]={0},visited[12]={0};
    MdVertex points[12];
    for(int e=0;e<12;e++) {
        int a=ends[e][0],b=ends[e][1];
        if((f[a]>0)==(f[b]>0))continue;
        active[e]=1;
        float t=f[a]/(f[a]-f[b]);
        points[e]=(MdVertex){0,0,0xffffffff,
            x+mix(corners[a][0],corners[b][0],t),
            y+mix(corners[a][1],corners[b][1],t),
            z+mix(corners[a][2],corners[b][2],t)};
        if(gradients)points[e].color=cave_light(mix(gradients[a][0],gradients[b][0],t),
            mix(gradients[a][1],gradients[b][1],t),mix(gradients[a][2],gradients[b][2],t));
    }
    for(int face=0;face<6;face++) {
        int edges[4],n=0;
        for(int k=0;k<4;k++)if(active[faces[face][k]])edges[n++]=faces[face][k];
        if(n==4) {
            float center=0;for(int k=0;k<4;k++)center+=f[face_corners[face][k]];
            if((center>0)!=(f[face_corners[face][0]]>0)) {
                int last=edges[3];for(int k=3;k>0;k--)edges[k]=edges[k-1];edges[0]=last;
            }
        }
        for(int k=0;k+1<n;k+=2) {
            int a=edges[k],b=edges[k+1];
            if(degree[a]>=2 || degree[b]>=2)return -1;
            links[a][degree[a]++]=b;links[b][degree[b]++]=a;
        }
    }
    int count=0;
    for(int first=0;first<12;first++)if(active[first] && !visited[first]) {
        int loop[12],n=0,at=first,previous=-1;
        do {
            if(n==12 || degree[at]!=2 || visited[at])return -1;
            loop[n++]=at;visited[at]=1;
            int next=links[at][0]==previous?links[at][1]:links[at][0];previous=at;at=next;
        } while(at!=first);
        for(int k=1;k+1<n;k++) {
            if(count+3>capacity)return -1;
            MdVertex a=points[loop[0]],b=points[loop[k]],c=points[loop[k+1]];
            float ux=b.x-a.x,uy=b.y-a.y,uz=b.z-a.z,vx=c.x-a.x,vy=c.y-a.y,vz=c.z-a.z;
            float nx=uy*vz-uz*vy,ny=uz*vx-ux*vz,nz=ux*vy-uy*vx;
            float norm=sqrtf(nx*nx+ny*ny+nz*nz);
            if(norm<.000001f)continue;
            unsigned color=gradients?0:cave_light(nx,ny,nz);
            MdVertex triangle[3]={a,b,c};
            for(int j=0;j<3;j++) {
                MdVertex p=triangle[j];if(!gradients)p.color=color;
                /* Planar projection chosen per triangle: rock on vertical and
                 * horizontal walls, no polar seam through branching chambers. */
                p.u=(fabsf(nx)>fabsf(ny)?p.y:p.x)*.3f;p.v=p.z*.3f;
                if(fabsf(nz)>fabsf(nx) && fabsf(nz)>fabsf(ny)) {p.u=p.x*.3f;p.v=p.y*.3f;}
                p.z=-p.z;out[count++]=p;
            }
        }
    }
    return count;
}
int cave_polygonize(const float f[8],MdVertex *out,int capacity,float x,float y,float z) {
    return polygonize(f,NULL,out,capacity,x,y,z);
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
    /* One extra future profile supplies the derivative of the upper plane. */
    for(int i=0;i<3;i++)cave_paths_step(&s->paths);
    return s;
}
void cave_destroy(CaveScene *s){free(s);}
CaveSlice *cave_prepare(CaveScene *s,const unsigned char bands[12],int level,unsigned long long now) {
    (void)level;
    float dt=s->motion.previous && now>=s->motion.previous?(now-s->motion.previous)*.000001f:0;
    s->motion.previous=now;if(dt>.1f)dt=.1f;
    float bass=(bands[0]+bands[1]+bands[2])/300.f;if(bass>1)bass=1;
    s->motion.bass+=(bass-s->motion.bass)*(dt*8);
    if(s->ready>=8) {
        float proposed=s->motion.travel+dt*(2+2*s->motion.bass);
        /* Never travel into an unprepared slab; audio is never stalled. */
        if(proposed<s->next-6)s->motion.travel=proposed;
    }
    int first=(int)floorf(s->motion.travel);
    if(s->next>=first+CAVE_SLICES)return NULL;
    int index=s->next;
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
    for(int y=0;y<CAVE_GRID;y++)for(int x=0;x<CAVE_GRID;x++) {
        float f[8]={planes[0][y][x],planes[0][y][x+1],planes[0][y+1][x+1],planes[0][y+1][x],
                    planes[1][y][x],planes[1][y][x+1],planes[1][y+1][x+1],planes[1][y+1][x]};
        float gradients[8][3];
        static const unsigned char corner[8][3]={{0,0,0},{1,0,0},{1,1,0},{0,1,0},{0,0,1},{1,0,1},{1,1,1},{0,1,1}};
        for(int i=0;i<8;i++)memcpy(gradients[i],normals[corner[i][2]][y+corner[i][1]][x+corner[i][0]],sizeof(gradients[i]));
        int n=polygonize(f,gradients,slice->vertices+used,CAVE_MAX_VERTICES-used,x-CAVE_GRID*.5f,y-CAVE_GRID*.5f,index);
        if(n<0){slice->count=0;return NULL;} /* Mathematically unreachable capacity guard. */
        used+=n;
    }
    slice->index=index;slice->count=used;s->next++;s->built++;
    if(s->ready<CAVE_SLICES)s->ready++;
    return slice;
}
