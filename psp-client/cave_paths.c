/* SPDX-License-Identifier: GPL-2.0-or-later
 * Independent reconstruction of Monkey's base 16-path oscillator controller.
 * See docs/MONKEY_GEOMETRY.md for addresses and intentionally omitted branches. */
#include "cave_paths.h"
#include <math.h>
#include <string.h>
static unsigned rng(unsigned *s){unsigned n=*s;n^=n<<13;n^=n>>17;n^=n<<5;return *s=n;}
static float clamp(float x,float low,float high){return x<low?low:x>high?high:x;}
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
    for(int i=0;i<CAVE_PATH_CACHE;i++)p->frames[i].index=-1;
}
void cave_paths_step(CavePaths *p) {
    int index=p->next;float t=(float)index,seed=p->seed;
    CavePathFrame *frame=&p->frames[index%CAVE_PATH_CACHE];
    float ex=.2f+.8f*powf(clamp(.5f+.2f*sinf(seed*3.4f+t*.0069f)+.3f*sinf(seed*1.3f+t*.0131f),0,1),.05f);
    float ey=.2f+.8f*powf(clamp(.5f+.2f*sinf(seed*2.1f+t*.0087f)+.3f*sinf(seed*1.6f+t*.0117f),0,1),.05f);
    float narrow=.075f*powf(clamp(.5f+.2f*sinf(t*.00159f+seed*.397f)+.3f*sinf(t*.00101f+seed*.117f),0,1),18);
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
        frame->radius[i]=clamp(p->radius[i]+2*narrow,.05f,.5f);
    }
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
