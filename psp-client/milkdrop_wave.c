/* SPDX-License-Identifier: GPL-2.0-or-later
 * New PSP implementation of the circular-wave geometry described by
 * MilkDrop 1/2 DrawWave mode 0. No original source copied. */
#include "milkdrop_wave.h"
#include <math.h>
volatile int md_wave_capture;
static volatile unsigned int wave_sequence;
static volatile short wave_right[MD_WAVE_SAMPLES];
static unsigned int wave_consumed;
void md_wave_forget(void) { wave_consumed=wave_sequence; }
void visualization_pcm_publish(const short *stereo, int frames) {
    if (!md_wave_capture || frames<=0) return;
    wave_sequence++;
    __sync_synchronize();
    for (int i=0;i<MD_WAVE_SAMPLES;i++)
        wave_right[i] = i<frames ? stereo[i*2+1] : 0;
    __sync_synchronize();
    wave_sequence++;
}
int md_wave_snapshot(short right[MD_WAVE_SAMPLES]) {
    short candidate[MD_WAVE_SAMPLES];
    unsigned int before=wave_sequence;
    if (!before || (before&1) || before==wave_consumed) return 0;
    __sync_synchronize();
    for (int i=0;i<MD_WAVE_SAMPLES;i++) candidate[i]=wave_right[i];
    __sync_synchronize();
    if (before!=wave_sequence) return 0;
    for (int i=0;i<MD_WAVE_SAMPLES;i++) right[i]=candidate[i];
    wave_consumed=before;
    return 1;
}
void md_wave_circle(MdVertex *vertices, const short *right, float scale,
                    float smoothing, float seconds, float aspect, unsigned int color) {
    MdDecor decor={.wave_x=.5f,.wave_y=.5f};
    md_wave_circle_style(vertices,right,scale,smoothing,seconds,aspect,color,&decor);
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
