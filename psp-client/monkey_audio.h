/* SPDX-License-Identifier: GPL-2.0-or-later
 * Reconstructed from the audited Monkey DLL's sound analysis and beat detector.
 * Addresses and differential fixtures: docs/MONKEY_AUDIO.md. */
#ifndef PSPSTREAMER_MONKEY_AUDIO_H
#define PSPSTREAMER_MONKEY_AUDIO_H
#include <math.h>
typedef struct {
    float average[3],history[3],threshold,target,last_peak;
    int ready,armed;
} MonkeyAudio;
static inline void monkey_audio_reset(MonkeyAudio *s) {
    for(int i=0;i<3;i++)s->average[i]=s->history[i]=1;
    s->threshold=5;s->target=s->last_peak=1.3f;s->armed=1;s->ready=1;
}
static inline void monkey_audio_bands(const float bins[512],float raw[3]) {
    /* 256 * pow(group/3, 2.1), truncated, NOT MilkDrop 2's ranges. */
    const int edges[]={0,25,109,256};
    const float scale[]={3.7037036418914795f,2.915452003479004f,3.3898305892944336f};
    for(int g=0;g<3;g++) {
        float sum=0;for(int i=edges[g];i<edges[g+1];i++)sum+=bins[i];
        raw[g]=sum/(edges[g+1]-edges[g])*scale[g];
    }
}
static inline int monkey_audio_detect(MonkeyAudio *s,int sensitivity,float immediate,float average,float dt) {
    float rate=powf(.999f,30*dt);
    if(sensitivity>8)rate-=.022f*powf((sensitivity-8)*.125f,3.4f);
    else if(sensitivity<8)rate+=(8-sensitivity)*.0000625f;
    s->threshold=s->threshold*rate+s->target*(1-rate);
    if(!s->armed) {
        if(average<s->threshold) {
            s->armed=1;
            s->threshold=(2.3f+(8-sensitivity)*.1f)*s->last_peak+.3f+(sensitivity-8)*.0125f;
            s->target=1.35f+(8-sensitivity)*.03125f;
        }
    } else if(immediate>s->threshold && immediate>.2f) {
        s->last_peak=immediate;s->armed=0;
        s->threshold=fminf(1,(.7f+(sensitivity-8)*.01875f)*immediate);
        s->target=1;return 1;
    }
    return 0;
}
static inline int monkey_audio_step(MonkeyAudio *s,const float raw[3],int sensitivity,float dt) {
    if(!s->ready)monkey_audio_reset(s);
    float immediate=0,average=0;
    for(int g=0;g<3;g++) {
        float rate=powf(raw[g]>s->average[g]?.2f:.5f,14*dt);
        s->average[g]=s->average[g]*rate+raw[g]*(1-rate);
        rate=powf(.96f,14*dt);
        s->history[g]=s->history[g]*rate+raw[g]*(1-rate);
        /* Silence may eventually underflow; do not produce NaN on the PSP. */
        if(s->history[g]>1e-20f){immediate+=raw[g]/s->history[g];average+=s->average[g]/s->history[g];}
    }
    return monkey_audio_detect(s,sensitivity,immediate*.3333f,average*.3333f,dt);
}
#endif
