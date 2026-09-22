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
static float noise3(const CaveScene *s,float x,float y,float z) {
    float fx=floorf(x),fy=floorf(y),fz=floorf(z);
    int ix=(int)fx&15,iy=(int)fy&15,iz=(int)fz&15;
    x-=fx;y-=fy;z-=fz;
    x=x*x*(3-2*x);y=y*y*(3-2*y);z=z*z*(3-2*z);
    float row[4];
    for(int k=0;k<2;k++)for(int j=0;j<2;j++) {
        int base=(((iz+k)&15)*16+((iy+j)&15))*16;
        row[k*2+j]=mix(s->noise[base+ix],s->noise[base+((ix+1)&15)],x);
    }
    return mix(mix(row[0],row[1],y),mix(row[2],row[3],y),z);
}
void cave_camera(float z,float *x,float *y) {
    *x=.9f*sinf(z*.10f);*y=.6f*cosf(z*.13f);
}
float cave_density(const CaveScene *s,float x,float y,float z) {
    float cx,cy;cave_camera(z,&cx,&cy);
    float field=0;
    /* Compact radial contribution seen at 0x10001f62: q*q-q+0.25,
     * q=(dx*dx+dy*dy)/radius^2, contributing only for q<0.5. */
    for(int i=0;i<3;i++) {
        float px=cx,py=cy,radius=5.6f;
        if(i) {
            px+= (i==1?1:-1)*(3.2f+1.8f*sinf(z*.19f+i));
            py+=1.8f*sinf(z*.23f+i*2);radius=3.5f;
        }
        float dx=x-px,dy=y-py,q=(dx*dx+dy*dy)/(radius*radius);
        if(q<.5f)field+=(q-.5f)*(q-.5f);
    }
    float noise=0,amplitude=1,frequency=.35f;
    for(int octave=0;octave<3;octave++) {
        noise+=amplitude*noise3(s,x*frequency+octave*3.1f,y*frequency,z*frequency);
        amplitude*=.51626223f;frequency*=1.937f;
    }
    if(noise<-1)noise=-1;
    if(noise>1)noise=1;
    const float iso=.08f;
    return field+iso*(-.22f+(noise+1)*.5f*(.36f+.22f))-iso;
}

/* Marching Cubes via face-edge connectivity, not a copied 256-case table.
 * Ambiguous faces use the same center-sign rule on either adjacent cell.
 * At most 12 intersections => 10 triangles. Positive field is cave interior. */
int cave_polygonize(const float f[8],MdVertex *out,int capacity,float x,float y,float z) {
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
            float light=.25f+.70f*fabsf((nx*.4f+ny*.6f+nz*.69282f)/norm);
            if(light>1)light=1;
            unsigned color=0xff000000U|(unsigned)(light*225)|((unsigned)(light*185)<<8)|((unsigned)(light*140)<<16);
            MdVertex triangle[3]={a,b,c};
            for(int j=0;j<3;j++) {
                MdVertex p=triangle[j];p.color=color;
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

CaveScene *cave_create(void) {
    CaveScene *s=memalign(64,sizeof(*s));if(!s)return NULL;
    memset(s,0,sizeof(*s));
    for(int i=0;i<CAVE_SLICES;i++)s->slices[i].index=-1;
    unsigned random=0x45319a7;
    for(int i=0;i<4096;i++) {
        random^=random<<13;random^=random>>17;random^=random<<5;
        s->noise[i]=(random&65535)/32767.5f-1;
    }
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
    CaveSlice *slice=&s->slices[index%CAVE_SLICES];
    static float planes[2][CAVE_GRID+1][CAVE_GRID+1];
    for(int k=0;k<2;k++)for(int y=0;y<=CAVE_GRID;y++)for(int x=0;x<=CAVE_GRID;x++)
        planes[k][y][x]=cave_density(s,x-CAVE_GRID*.5f,y-CAVE_GRID*.5f,index+k);
    int used=0;
    for(int y=0;y<CAVE_GRID;y++)for(int x=0;x<CAVE_GRID;x++) {
        float f[8]={planes[0][y][x],planes[0][y][x+1],planes[0][y+1][x+1],planes[0][y+1][x],
                    planes[1][y][x],planes[1][y][x+1],planes[1][y+1][x+1],planes[1][y+1][x]};
        int n=cave_polygonize(f,slice->vertices+used,CAVE_MAX_VERTICES-used,x-CAVE_GRID*.5f,y-CAVE_GRID*.5f,index);
        if(n<0){slice->count=0;return NULL;} /* Mathematically unreachable capacity guard. */
        used+=n;
    }
    slice->index=index;slice->count=used;s->next++;s->built++;
    if(s->ready<CAVE_SLICES)s->ready++;
    return slice;
}
