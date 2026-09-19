#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "milkdrop_preset.h"
static unsigned int calls;
static float counted_sin(float x){calls++;return sinf(x);}
static float counted_cos(float x){calls++;return cosf(x);}
#define sinf counted_sin
#define cosf counted_cos
#define pm_trig cached_trig
#include "preset_trig.h"
#undef pm_trig
static int bypass;
static float pm_trig(float x,int cosine) {
    return bypass?(cosine?counted_cos(x):counted_sin(x)):cached_trig(x,cosine);
}
#include "preset_math.c"
#undef sinf
#undef cosf
static void exact(float x) {
    float s=sinf(x),c=cosf(x),a=cached_trig(x,0),b=cached_trig(x,1);
    assert(!memcmp(&s,&a,sizeof(s)) && !memcmp(&c,&b,sizeof(c)));
}
int main(int argc,char **argv) {
    pm_trig_reset();calls=0;
    for(int i=0;i<1000;i++)exact(.12345f);
    assert(calls==2);
    exact(0.0f);exact(-0.0f);exact(0.0f);
    uint32_t seed=1234567;
    for(int i=0;i<20000;i++) {
        seed=seed*1664525U+1013904223U;
        float x;memcpy(&x,&seed,sizeof(x));if(isfinite(x))exact(x);
    }
    pm_trig_reset();calls=0;exact(.12345f);assert(calls==2);
    for(int arg=1;arg<argc;arg++) {
        static MdFilePreset preset;
        static MdPresetState state,reference_state[24];
        static MdWaveGeometry geometry[MD_CUSTOM_WAVES],reference[24][MD_CUSTOM_WAVES];
        MdFileError error;
        assert(md_load_preset(argv[arg],&preset,&error)==MD_FILE_OK);
        short pcm[576]={0};float spectrum[512];
        for(int i=0;i<512;i++)spectrum[i]=(i%19)*.025f;
        MdSignal signal={0};for(int i=0;i<MD_SIGNAL_COUNT;i++)signal.values[i]=.7f;
        unsigned int baseline=0;
        for(int run=0;run<2;run++) {
            bypass=!run;pm_reset_globals();calls=0;memset(&state,0,sizeof(state));
            memset(geometry,0,sizeof(geometry));
            for(int frame=0;frame<24;frame++) {
                MdPreset warp;MdDecor decor;unsigned int color;
                float time=frame*.2f;
                assert(md_eval_preset_state(&preset,time,&signal,&state,&warp,&color,&decor,&error)==MD_FILE_OK);
                assert(md_eval_custom_waves(&preset,time,&signal,pcm,pcm,spectrum,spectrum,&state,geometry,&error)==MD_FILE_OK);
                if(!run){reference_state[frame]=state;memcpy(reference[frame],geometry,sizeof(geometry));}
                else {
                    assert(!memcmp(&state,&reference_state[frame],sizeof(state)));
                    assert(!memcmp(geometry,reference[frame],sizeof(geometry)));
                }
            }
            if(!run)baseline=calls;
            else {
                printf("%s: VM trig calls %u -> %u; identical wave/state output\n",argv[arg],baseline,calls);
                fflush(stdout);assert(calls<=baseline);
                if(strstr(argv[arg],"Cauldron"))assert(calls<baseline);
            }
        }
    }
    return 0;
}
