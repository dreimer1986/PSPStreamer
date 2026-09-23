#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static int music_preset_seconds=60;
#include "visual_options.h"
CaveOptions cave_options={1,1,1,1,1,8,8,-1,100,0};
int main(void) {
    assert(!visual_option_parse("unknown=1"));
    assert(visual_option_parse("cave_amplitude=999999999999999999999999"));
    assert(cave_options.amplitude==16);
    assert(visual_option_parse("cave_amplitude=-10") && !cave_options.amplitude);
    assert(visual_option_parse("cave_amplitude=8x") && !cave_options.amplitude);
    for(int i=0;i<VISUAL_OPTION_COUNT;i++) {
        char line[128];VisualOption *o=visual_options+i;
        snprintf(line,sizeof(line),"%s=%d",o->key,o->maximum);
        assert(visual_option_parse(line) && *o->value==o->maximum);
        snprintf(line,sizeof(line),"%s=%d",o->key,o->minimum);
        assert(visual_option_parse(line) && *o->value==o->minimum);
    }
    preset_random_seconds=10;
    for(int i=0;i<1000;i++){unsigned long long t=preset_interval_us();assert(t>=60000000 && t<70000000);}
    preset_hard_threshold=250;preset_hard_seconds=60;
    PresetCuts cuts={0};unsigned char bands[12];memset(bands,20,sizeof(bands));
    assert(!preset_cut_due(&cuts,bands,1000000));assert(cuts.threshold==5);
    float initial=cuts.threshold;
    assert(!preset_cut_due(&cuts,bands,1010000) && cuts.threshold==initial);
    assert(!preset_cut_due(&cuts,bands,1050000));assert(cuts.threshold<initial && cuts.threshold>2.5f);
    cuts.threshold=1.3f;memset(bands,100,sizeof(bands));
    assert(preset_cut_due(&cuts,bands,1100000));assert(fabsf(cuts.threshold-2.6f)<1e-6f);
    puts("Visual options: bounds, serialization keys, random intervals and source hard-cut threshold law OK");
}
