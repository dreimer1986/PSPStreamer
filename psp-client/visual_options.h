/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef PSPSTREAMER_VISUAL_OPTIONS_H
#define PSPSTREAMER_VISUAL_OPTIONS_H
#include "cave_visual.h"
#include "language.h"
#include "milkdrop_signal.h"
#include <math.h>
static int preset_random_seconds=10,preset_hard_cuts=0,preset_hard_threshold=250,preset_hard_seconds=60;
typedef struct {const char *key;TextId label;int *value,minimum,maximum,step;} VisualOption;
static VisualOption visual_options[]={
    {"cave_fog",TXT_CAVE_FOG,&cave_options.fog,0,1,1},
    {"cave_multitexture",TXT_CAVE_TEXTURE,&cave_options.multitexture,0,1,1},
    {"cave_hair",TXT_CAVE_HAIR,&cave_options.hair,0,1,1},
    {"cave_transparent_hair",TXT_CAVE_ALPHA,&cave_options.transparent_hair,0,1,1},
    {"cave_beat",TXT_CAVE_BEAT,&cave_options.beat,0,1,1},
    {"cave_sensitivity",TXT_CAVE_SENSITIVITY,&cave_options.sensitivity,0,16,1},
    {"cave_amplitude",TXT_CAVE_AMPLITUDE,&cave_options.amplitude,0,16,1},
    {"cave_style",TXT_CAVE_STYLE,&cave_options.style,-1,8,1},
    {"preset_random_seconds",TXT_PRESET_RANDOM,&preset_random_seconds,0,120,5},
    {"preset_hard_cuts",TXT_PRESET_HARD,&preset_hard_cuts,0,1,1},
    {"preset_hard_threshold",TXT_PRESET_THRESHOLD,&preset_hard_threshold,125,400,10},
    {"preset_hard_seconds",TXT_PRESET_HALFLIFE,&preset_hard_seconds,5,180,5}
};
#define VISUAL_OPTION_COUNT ((int)(sizeof(visual_options)/sizeof(visual_options[0])))
#define VISUAL_CAVE_OPTIONS 8
static int visual_option_parse(const char *line) {
    for(int i=0;i<VISUAL_OPTION_COUNT;i++) {
        VisualOption *o=&visual_options[i];size_t n=strlen(o->key);
        if(strncmp(line,o->key,n)||line[n]!='=')continue;
        char *end;long value=strtol(line+n+1,&end,10);
        if(end==line+n+1 || *end)return 1;
        if(value<o->minimum)value=o->minimum;
        if(value>o->maximum)value=o->maximum;
        *o->value=(int)value;return 1;
    }
    return 0;
}
static unsigned long long preset_interval_us(void) {
    return (unsigned long long)music_preset_seconds*1000000ULL+
        (unsigned long long)preset_random_seconds*(rand()%1000)*1000ULL;
}
typedef struct {MdSignalState audio;float threshold;unsigned long long tick;} PresetCuts;
static int preset_cut_due(PresetCuts *s,const unsigned char bands[12],unsigned long long now) {
    if(s->tick && now-s->tick<50000)return 0;
    float base=preset_hard_threshold*.01f;
    float dt=s->tick?(now-s->tick)*.000001f:0;
    if(!s->tick)s->threshold=base*2;
    s->tick=now;md_signal_update(&s->audio,bands,0,now);
    if(s->audio.signal.values[7]+s->audio.signal.values[8]+s->audio.signal.values[9]>s->threshold*3) {
        s->threshold*=2;return 1;
    }
    s->threshold=base+(s->threshold-base)*expf(-1.3863f*dt/preset_hard_seconds);
    return 0;
}
#endif
