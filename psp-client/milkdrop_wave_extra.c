/* SPDX-License-Identifier: GPL-2.0-or-later
 * Bounded PSP implementation of MilkDrop DrawWave modes 2,3,5,6,7,8.
 * Original geometry semantics, PSP sample normalization and FFT front-end. */
#include "milkdrop_wave.h"
#include <math.h>

/* UI-thread-only scratch, never shared with the PCM producer. */
static float real_part[1024],imag_part[1024],window[1024];
static int fft_ready;
/* MilkDrop SmoothWave coefficients. Preserve endpoints and input colors;
 * clamp interpolation overshoot to the valid feedback rectangle. */
int md_wave_smooth(MdVertex *out,const MdVertex *in,int count) {
    if(count<2 || count>512) return 0;
    for(int i=0;i<count-1;i++) {
        int below=i?i-1:0,above=i+2<count?i+2:count-1;
        out[2*i]=in[i]; out[2*i+1]=in[i];
        float x=(-.15f*in[below].x+1.15f*in[i].x+1.15f*in[i+1].x-.15f*in[above].x)*.5f;
        float y=(-.15f*in[below].y+1.15f*in[i].y+1.15f*in[i+1].y-.15f*in[above].y)*.5f;
        out[2*i+1].x=fminf(256,fmaxf(0,x)); out[2*i+1].y=fminf(256,fmaxf(0,y));
    }
    out[2*count-2]=in[count-1]; return 2*count-1;
}
void md_wave_spectrum(const short *samples,float bins[512]) {
    if(!fft_ready) {
        for(int i=0;i<1024;i++) window[i]=.5f-.5f*cosf(6.283185307f*i/1023);
        fft_ready=1;
    }
    for(int i=0;i<1024;i++) { real_part[i]=samples[i]/32768.0f*window[i]; imag_part[i]=0; }
    for(int i=1,j=0;i<1024;i++) {
        int bit=512;
        for(;j&bit;bit>>=1) j^=bit;
        j^=bit;
        if(i<j) { float t=real_part[i]; real_part[i]=real_part[j]; real_part[j]=t; }
    }
    for(int length=2;length<=1024;length*=2) {
        float angle=-6.283185307f/length,wr=cosf(angle),wi=sinf(angle);
        for(int base=0;base<1024;base+=length) {
            float ur=1,ui=0;
            for(int j=0;j<length/2;j++) {
                int a=base+j,b=a+length/2;
                float vr=real_part[b]*ur-imag_part[b]*ui,vi=real_part[b]*ui+imag_part[b]*ur;
                real_part[b]=real_part[a]-vr; imag_part[b]=imag_part[a]-vi;
                real_part[a]+=vr; imag_part[a]+=vi;
                float next=ur*wr-ui*wi; ui=ur*wi+ui*wr; ur=next;
            }
        }
    }
    for(int i=0;i<512;i++) bins[i]=sqrtf(real_part[i]*real_part[i]+imag_part[i]*imag_part[i])/256;
}
static void endpoints(float x,float angle,float ex[2],float ey[2]) {
    float dx=cosf(angle),dy=sinf(angle);
    ex[0]=x*cosf(angle+1.57f)-3*dx; ex[1]=ex[0]+6*dx;
    ey[0]=x*sinf(angle+1.57f)-3*dy; ey[1]=ey[0]+6*dy;
    for(int i=0;i<2;i++) for(int edge=0;edge<4;edge++) {
        float *axis=edge<2?ex:ey, bound=(edge&1)?-1.1f:1.1f;
        if((edge&1)?axis[i]<bound:axis[i]>bound) {
            float denominator=axis[i]-axis[1-i];
            if(fabsf(denominator)<1e-8f) continue;
            float t=(bound-axis[1-i])/denominator;
            ex[i]=ex[1-i]+(ex[i]-ex[1-i])*t;
            ey[i]=ey[1-i]+(ey[i]-ey[1-i])*t;
        }
    }
}
int md_wave_extra(MdVertex *v,int mode,const short *right,const short *left,
                  const short *spectrum,float scale,float smoothing,float seconds,
                  float aspect,unsigned int color,const MdDecor *d,int *split) {
    float r[576],l[576],bins[512];
    *split=0;
    if(mode!=8) {
        r[0]=right[0]*scale/32768; l[0]=left[0]*scale/32768;
        for(int i=1;i<576;i++) {
            r[i]=right[i]*scale/32768*(1-smoothing)+r[i-1]*smoothing;
            l[i]=left[i]*scale/32768*(1-smoothing)+l[i-1]*smoothing;
        }
    }
    if(mode==2 || mode==3 || mode==5) {
        float c=cosf(seconds*.3f),s=sinf(seconds*.3f);
        for(int i=0;i<480;i++) {
            float x=r[i],y=l[i+32];
            if(mode==5) {
                float a=r[i]*l[i+32]+l[i]*r[i+32],b=r[i]*r[i]-l[i+32]*l[i+32];
                x=a*c-b*s; y=a*s+b*c;
            }
            v[i]=(MdVertex){0,0,color,d->wave_x*256+128*x*aspect,(1-d->wave_y)*256-128*y,0};
        }
        return 480;
    }
    if(mode<6 || mode>8) return 0;
    int count=mode==8?256:170, offset=(480-count)/2;
    float ex[2],ey[2]; endpoints(d->wave_x*2-1,1.57f*d->wave_param,ex,ey);
    float dx=(ex[1]-ex[0])/count,dy=(ey[1]-ey[0])/count;
    float angle=atan2f(dy,dx)+1.57f,px=cosf(angle),py=sinf(angle);
    if(mode==8) md_wave_spectrum(spectrum,bins);
    for(int channel=0;channel<(mode==7?2:1);channel++) for(int i=0;i<count;i++) {
        float displacement;
        if(mode==8) displacement=.1f*logf(fmaxf(1e-6f,bins[i*2]+bins[i*2+1]));
        else {
            displacement=.25f*(channel?r[i+offset]:l[i+offset]);
            if(mode==7) displacement+=(channel?-1:1)*d->wave_y*d->wave_y;
        }
        v[channel*count+i]=(MdVertex){0,0,color,128+128*(ex[0]+dx*i+px*displacement),
                                     128-128*(ey[0]+dy*i+py*displacement),0};
    }
    if(mode==7) *split=count;
    return count*(mode==7?2:1);
}
