/* SPDX-License-Identifier: GPL-2.0-or-later
 * Independent reconstruction of Monkey's oscillator/spline path controller.
 * See docs/MONKEY_GEOMETRY.md for verified behavior and PSP adaptations. */
#include "cave_paths.h"
#include <math.h>
#include <string.h>
static unsigned rng(unsigned *s){unsigned n=*s;n^=n<<13;n^=n>>17;n^=n<<5;return *s=n;}
static float clamp(float x,float low,float high){return x<low?low:x>high?high:x;}
/* 0x10012340: repeat cosine easing, interpolate the fractional iteration.
 * Negative amounts use inverse cosine easing (source ambient-light branch). */
float cave_path_shape(float value,float amount) {
    value=clamp(value,0,1);int inverse=amount<0;amount=clamp(fabsf(amount),0,4);
    int count=(int)amount;
    for(int i=0;i<count;i++)value=inverse?acosf(clamp(1-2*value,-1,1))/3.141592654f:.5f-.5f*cosf(value*3.141592654f);
    float next=inverse?acosf(clamp(1-2*value,-1,1))/3.141592654f:.5f-.5f*cosf(value*3.141592654f);
    return value+(next-value)*(amount-count);
}
static float spline_interval(unsigned *random,float interval,float spread) {
    /* 0x1000a440/0x1000a600: interval * 2^((2*r/3996-1)*spread). */
    return interval*exp2f(((rng(random)%3997)/1998.f-1)*spread);
}
static float spline_value(unsigned *random,float shape) {
    return 2*cave_path_shape((rng(random)%3997)/3996.f,shape)-1;
}
float cave_spline_sample(CaveSpline *s,unsigned *random,float t,float interval,float spread,float shape) {
    if(!s->ready || t<s->time[1] || t>s->time[3]) {
        s->time[1]=t;s->time[0]=t-spline_interval(random,interval,spread);
        s->time[2]=t+spline_interval(random,interval,spread);
        s->time[3]=s->time[2]+spline_interval(random,interval,spread);
        for(int i=0;i<4;i++)s->value[i]=spline_value(random,shape);
        s->ready=1;
    } else if(t>s->time[2]) {
        for(int i=0;i<3;i++){s->time[i]=s->time[i+1];s->value[i]=s->value[i+1];}
        s->time[3]=s->time[2]+spline_interval(random,interval,spread);
        s->value[3]=spline_value(random,shape);
    }
    /* Nonuniform four-knot Hermite cubic (0x1000a380). */
    float h=s->time[2]-s->time[1],u=(t-s->time[1])/h;
    float m0=(s->value[2]-s->value[0])/(s->time[2]-s->time[0])*h;
    float m1=(s->value[3]-s->value[1])/(s->time[3]-s->time[1])*h;
    return ((2*s->value[1]-2*s->value[2]+m0+m1)*u+
            (-3*s->value[1]+3*s->value[2]-2*m0-m1))*u*u+m0*u+s->value[1];
}
void cave_paths_init(CavePaths *p,unsigned seed) {
    memset(p,0,sizeof(*p));if(!seed)seed=1;
    p->seed=rng(&seed)%997;
    p->seed+=(rng(&seed)%100)*.01f;
    for(int i=0;i<CAVE_PATHS;i++) {
        p->radius[i]=.1f+(rng(&seed)&1023)*.0001640625f;
        p->phase[i][0]=(rng(&seed)%628)*.01f;
        p->phase[i][1]=(rng(&seed)%628)*.01f;
    }
    p->radius[0]=.189f+.1f*p->radius[0];
    p->random=seed;
    p->roughness=.015f; /* PSP profile selection, not the desktop default. */
    for(int i=0;i<CAVE_PATH_CACHE;i++)p->frames[i].index=-1;
}
void cave_paths_perturb(CavePathFrame *frame,unsigned *random,float amount) {
    amount=clamp(amount,0,1);
    for(int i=1;i<CAVE_PATHS;i+=2) {
        /* The inspected 0x10004994..0x100049b3 actually uses dx twice,
         * not dx*dx+dy*dy. Preserve this source behavior deliberately. */
        float dx=frame->x[i]-frame->x[0];
        float distance=sqrtf(2*dx*dx);
        float gain=distance>.3f?0:distance>.1f?(.3f-distance)*5:1;
        for(int axis=0;axis<2;axis++) {
            unsigned a=rng(random)%173,b=rng(random)%173;
            float delta=((a+b)/172.f-1)*amount*gain;
            float *value=axis?&frame->y[i]:&frame->x[i];
            *value=clamp(*value+delta,0,1);
        }
        float radial=((rng(random)%173)/86.f-1)*amount*gain;
        frame->radius[i]=clamp(frame->radius[i]*(1+radial),.05f,.5f);
    }
}
void cave_paths_step(CavePaths *p) {
    int index=p->next;float t=(float)index,seed=p->seed;
    CavePathFrame *frame=&p->frames[index%CAVE_PATH_CACHE];
    float ex=.2f+.8f*powf(clamp(.5f+.2f*sinf(seed*3.4f+t*.0069f)+.3f*sinf(seed*1.3f+t*.0131f),0,1),.05f);
    float ey=.2f+.8f*powf(clamp(.5f+.2f*sinf(seed*2.1f+t*.0087f)+.3f*sinf(seed*1.6f+t*.0117f),0,1),.05f);
    float narrow=.075f*powf(clamp(.5f+.2f*sinf(t*.00159f+seed*.397f)+.3f*sinf(t*.00101f+seed*.117f),0,1),18);
    float shape=.45f+.6f*powf(clamp(.5f+.2f*sinf(t*.00573f+seed*.93f)+.3f*sinf(t*.00411f+seed*.382f),0,1),.4f);
    float blend=cave_path_shape(.25f+.35f*sinf(t*.00972f+seed*.345f)+.45f*sinf(t*.01313f+seed*.623f),2);
    int shift[4]={(int)(seed*-.71f),(int)(seed*-.99f),(int)(seed*-.62f),(int)(seed*-.83f)};
    for(int i=0;i<CAVE_PATHS;i++) {
        int a=(221*i+763)*i,b=(423*i+323)*i,c=(569*i+561)*i;
        int d=(322*i+453)*i,e=(327*i+411)*i,f=(267*i+351)*i;
        float sx=0,sy=0,total=0;
        for(int k=0;k<4;k++) {
            float dx=((a-shift[0]+k*272+41)%50)*.002f-.01f+((b-shift[1]+k*553+69)%50)*.002f;
            float dy=((c-shift[2]+k*368+82)%50)*.002f-.01f+((d-shift[3]+k*543+36)%50)*.002f;
            float angle=(((e+k*615+94)%50)*.02f+1)*.17f*t+((f+k*473+31)%50)*.02f*6.28f;
            float weight=powf(.52f+.48f*sinf(angle),i?1.8f:3.8f);
            sx+=weight*dx;sy+=weight*dy;total+=weight;
            a+=131*i;b+=93*i;c+=117*i;d+=171*i;e+=313*i;f+=241*i;
        }
        /* Source movement multiplier fixed at 1 for this PSP profile. */
        if(total>0) {
            p->phase[i][0]+=clamp(sx/total,0,i?.12f:.06f);
            p->phase[i][1]+=clamp(sy/total,0,i?.12f:.06f);
        }
        /* Equivalent phase reduction avoids losing motion precision over hours. */
        for(int j=0;j<2;j++)if(p->phase[i][j]>=6.283185307f)p->phase[i][j]-=6.283185307f;
        frame->x[i]=.5f+(.335f-narrow)*ex*sinf(p->phase[i][0]+i*11.7f-i*i*.351f);
        frame->y[i]=.5f+(.335f-narrow)*ey*sinf(p->phase[i][1]+i*14.7f+i*i*.755f);
        if(blend>0) {
            float interval=i?26:fminf(100,26*1.7f*(1+100*narrow));
            float alt[3];
            for(int axis=0;axis<3;axis++)alt[axis]=cave_spline_sample(&p->alternate[i][axis],&p->random,t,interval,i?.5f:.3f,shape);
            frame->x[i]+=blend*(.5f+(.335f-narrow)*ex*alt[0]-frame->x[i]);
            frame->y[i]+=blend*(.5f+(.335f-narrow)*ey*alt[1]-frame->y[i]);
            /* The DLL advances the third generator but discards its result. */
        }
        /* Cubics can overshoot their random knots; field bounds, not knot
         * bounds, are the source's final safeguard. */
        frame->x[i]=clamp(frame->x[i],0,1);frame->y[i]=clamp(frame->y[i],0,1);
        frame->radius[i]=clamp(p->radius[i]+2*narrow,.05f,.5f);
    }
    cave_paths_perturb(frame,&p->random,p->roughness);
    frame->index=index;p->next++;
}
int cave_paths_sample(const CavePaths *p,float z,CavePathFrame *out,CavePathFrame *derivative) {
    if(!isfinite(z) || z<0 || z>=2147483000.f)return 0;
    int index=(int)floorf(z);float f=z-index;
    const CavePathFrame *a=&p->frames[index%CAVE_PATH_CACHE],*b=&p->frames[(index+1)%CAVE_PATH_CACHE];
    if(a->index!=index || b->index!=index+1)return 0;
    for(int i=0;i<CAVE_PATHS;i++) {
        float dx=b->x[i]-a->x[i],dy=b->y[i]-a->y[i],dr=b->radius[i]-a->radius[i];
        out->x[i]=a->x[i]+f*dx;out->y[i]=a->y[i]+f*dy;out->radius[i]=a->radius[i]+f*dr;
        if(derivative){derivative->x[i]=dx;derivative->y[i]=dy;derivative->radius[i]=dr;}
    }
    out->index=index;if(derivative)derivative->index=index;
    return 1;
}
