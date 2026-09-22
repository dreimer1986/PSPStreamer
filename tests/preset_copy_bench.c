/* Focused deterministic frame comparison/benchmark; not a PSP FPS estimate. */
#define _POSIX_C_SOURCE 200809L
#include "milkdrop_preset.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static uint32_t hash=2166136261u;
static void digest(const void *data,size_t size) {
    const unsigned char *p=data;
    while(size--)hash=(hash^*p++)*16777619u;
}
int main(int argc,char **argv) {
    assert(argc==3);
    static MdFilePreset preset;
    static MdPresetState state;
    static MdWaveGeometry waves[MD_CUSTOM_WAVES];
    static MdPreset points[MD_GRID_POINTS];
    MdFileError error;
    assert(md_load_preset(argv[1],&preset,&error)==MD_FILE_OK);
    short right[576],left[576];float spectrum[512];
    for(int i=0;i<576;i++){right[i]=(short)((i*97)%24000-12000);left[i]=(short)((i*43)%20000-10000);}
    for(int i=0;i<512;i++)spectrum[i]=.2f/(i+1);
    MdSignal signal={0};for(int i=0;i<MD_SIGNAL_COUNT;i++)signal.values[i]=i<7?.5f:1;
    int frames=atoi(argv[2]);assert(frames>0 && frames<=1200);
    pm_reset_globals();
    struct timespec start,end;clock_gettime(CLOCK_MONOTONIC,&start);
    for(int i=0;i<frames;i++) {
        MdPreset warp;MdDecor decor;unsigned int color;
        float seconds=i/30.0f;
        assert(md_eval_preset_state(&preset,seconds,&signal,&state,&warp,&color,&decor,&error)==MD_FILE_OK);
        digest(&warp,sizeof(warp));digest(&color,sizeof(color));
        if(preset.pixel_program.count) {
            assert(md_eval_pixel_grid(&preset,&warp,seconds,&signal,&state,points,&error)==MD_FILE_OK);
            digest(points,sizeof(points));
        }
        assert(md_eval_custom_waves(&preset,seconds,&signal,right,left,spectrum,spectrum,&state,waves,&error)==MD_FILE_OK);
        for(int slot=0;slot<MD_CUSTOM_WAVES;slot++) {
            digest(&waves[slot].count,sizeof(waves[slot].count));
            digest(waves[slot].vertices,waves[slot].count*sizeof(MdVertex));
        }
        int fuel=pm_frame_remaining();digest(&fuel,sizeof(fuel));
    }
    clock_gettime(CLOCK_MONOTONIC,&end);
    printf("frames=%d hash=%08x milliseconds=%.3f state_bytes=%zu\n",frames,hash,
        (end.tv_sec-start.tv_sec)*1000.0+(end.tv_nsec-start.tv_nsec)/1000000.0,sizeof(state));
    md_free_preset(&preset);
}
