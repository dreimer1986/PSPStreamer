/* SPDX-License-Identifier: GPL-2.0-or-later
 * New PSP implementation of MilkDrop 1/2 DrawWave modes 0, 1 and 4.
 * No original source copied. */
#include "milkdrop_wave.h"
#include <math.h>
#include <stddef.h>
volatile int md_wave_capture;
static volatile unsigned int wave_sequence;
static volatile int wave_published_kind;
static volatile short wave_right[MD_WAVE_SAMPLES];
static volatile short wave_left[MD_WAVE_SAMPLES];
static volatile short spectrum_left[MD_SPECTRUM_SAMPLES];
static unsigned int wave_consumed;
void md_wave_forget(void) { wave_consumed=wave_sequence; }
void visualization_pcm_publish(const short *stereo, int frames) {
    int capture=md_wave_capture;
    if (!capture || frames<=0) return;
    wave_sequence++;
    __sync_synchronize();
    wave_published_kind=capture;
    if(capture==3) {
        for(int i=0;i<MD_SPECTRUM_SAMPLES;i++) spectrum_left[i]=i<frames?stereo[i*2]:0;
    } else {
    for (int i=0;i<MD_WAVE_SAMPLES;i++) {
        wave_right[i] = i<frames ? stereo[i*2+1] : 0;
        if(capture==2) wave_left[i] = i<frames ? stereo[i*2] : 0;
    }
    }
    __sync_synchronize();
    wave_sequence++;
}
int md_wave_snapshot(short right[MD_WAVE_SAMPLES]) {
    return md_wave_snapshot_stereo(right,NULL);
}
int md_spectrum_snapshot(short left[MD_SPECTRUM_SAMPLES]) {
    short candidate[MD_SPECTRUM_SAMPLES];
    unsigned int before=wave_sequence;
    if(!before || (before&1) || before==wave_consumed) return 0;
    __sync_synchronize();
    if(wave_published_kind!=3) return 0;
    for(int i=0;i<MD_SPECTRUM_SAMPLES;i++) candidate[i]=spectrum_left[i];
    __sync_synchronize();
    if(before!=wave_sequence) return 0;
    for(int i=0;i<MD_SPECTRUM_SAMPLES;i++) left[i]=candidate[i];
    wave_consumed=before;
    return 1;
}
int md_wave_snapshot_stereo(short right[MD_WAVE_SAMPLES], short left[MD_WAVE_SAMPLES]) {
    short candidate[MD_WAVE_SAMPLES], candidate_left[MD_WAVE_SAMPLES];
    unsigned int before=wave_sequence;
    if (!before || (before&1) || before==wave_consumed) return 0;
    __sync_synchronize();
    if(left ? wave_published_kind!=2 : (wave_published_kind!=1 && wave_published_kind!=2)) return 0;
    for (int i=0;i<MD_WAVE_SAMPLES;i++) {
        candidate[i]=wave_right[i];
        if(left) candidate_left[i]=wave_left[i];
    }
    __sync_synchronize();
    if (before!=wave_sequence) return 0;
    for (int i=0;i<MD_WAVE_SAMPLES;i++) {
        right[i]=candidate[i];
        if(left) left[i]=candidate_left[i];
    }
    wave_consumed=before;
    return 1;
}
/* MilkDrop mode 4: left-channel script with delayed right-channel X motion.
 * The 512-wide feedback texture limits the original path to 512/3 points.
 * These are open lines, not the closed circle used by mode 0. */
int md_wave_script(MdVertex *v,const short *right,const short *left,
                   float scale,float smoothing,unsigned int color,const MdDecor *d) {
    float r[MD_WAVE_SAMPLES],l[MD_WAVE_SAMPLES];
    const int count=512/3, offset=(MD_WAVE_SAMPLES-count)/2;
    float weight=.45f+.5f*(d->wave_param*.5f+.5f);
    float gain=scale/32768.0f;
    r[0]=right[0]*gain; l[0]=left[0]*gain;
    for(int i=1;i<MD_WAVE_SAMPLES;i++) {
        r[i]=right[i]*gain*(1-smoothing)+r[i-1]*smoothing;
        l[i]=left[i]*gain*(1-smoothing)+l[i-1]*smoothing;
    }
    for(int i=0;i<count;i++) {
        float x=d->wave_x*256-128+256.0f*i/count+128*.44f*r[i+offset+25];
        float y=(1-d->wave_y)*256-128*.47f*l[i+offset];
        if(i>1) {
            x=x*(1-weight)+weight*(2*v[i-1].x-v[i-2].x);
            y=y*(1-weight)+weight*(2*v[i-1].y-v[i-2].y);
        }
        v[i]=(MdVertex){0,0,color,x,y,0};
    }
    return count;
}
void md_wave_circle(MdVertex *vertices, const short *right, float scale,
                    float smoothing, float seconds, float aspect, unsigned int color) {
    MdDecor decor={.wave_x=.5f,.wave_y=.5f};
    md_wave_circle_style(vertices,right,scale,smoothing,seconds,aspect,color,&decor);
}
/* Mode 1: right-channel radius, delayed left-channel angle and time rotation.
 * Keep the original 240-point open path: unlike mode 0 it has no seam. */
int md_wave_spiral(MdVertex *v,const short *right,const short *left,
                   float scale,float smoothing,float seconds,float aspect,
                   unsigned int color,const MdDecor *d) {
    float r=0,l=0, left_samples[272];
    float gain=scale/32768.0f;
    for(int i=0;i<272;i++) {
        l=i ? left[i]*gain*(1-smoothing)+l*smoothing : left[i]*gain;
        left_samples[i]=l;
    }
    for(int i=0;i<240;i++) {
        r=i ? right[i]*gain*(1-smoothing)+r*smoothing : right[i]*gain;
        float radius=.53f+.43f*r+d->wave_param;
        float angle=left_samples[i+32]*1.57f+seconds*2.3f;
        v[i]=(MdVertex){0,0,color,d->wave_x*256+128*radius*cosf(angle)*aspect,
                       (1-d->wave_y)*256-128*radius*sinf(angle),0};
    }
    return 240;
}
void md_wave_circle_style(MdVertex *vertices, const short *right, float scale,
                    float smoothing, float seconds, float aspect, unsigned int color,
                    const MdDecor *decor) {
    float samples[MD_WAVE_SAMPLES];
    static float circle_x[240],circle_y[240],seam[24];
    static int ready;
    if (!ready) {
        for(int i=0;i<240;i++) {
            float angle=i*(6.28f/239);
            circle_x[i]=cosf(angle); circle_y[i]=sinf(angle);
        }
        for(int i=0;i<24;i++) seam[i]=.5f-.5f*cosf(i/24.0f*3.1416f);
        ready=1;
    }
    samples[0]=right[0]*(scale/32768.0f);
    for(int i=1;i<MD_WAVE_SAMPLES;i++)
        samples[i]=right[i]*(scale/32768.0f)*(1-smoothing)+samples[i-1]*smoothing;
    float c=cosf(seconds*.2f),s=sinf(seconds*.2f);
    for(int i=0;i<240;i++) {
        float radius=.5f+.4f*samples[i+120];
        if(i<24) radius=(.5f+.4f*samples[i+360])*(1-seam[i])+radius*seam[i];
        radius+=decor->wave_param;
        vertices[i]=(MdVertex){0,0,color,
            decor->wave_x*256+128*radius*(circle_x[i]*c-circle_y[i]*s)*aspect,
            (1-decor->wave_y)*256-128*radius*(circle_x[i]*s+circle_y[i]*c),0};
    }
    vertices[240]=vertices[0];
}
