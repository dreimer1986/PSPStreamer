/* Renderer-thread-only, opt-in wall-clock profiling. No playback-time I/O. */
#ifndef PSPSTREAMER_MILKDROP_PROFILE_H
#define PSPSTREAMER_MILKDROP_PROFILE_H
#include "milkdrop_wave.h"
#define MD_PROFILE_PHASES 7
#define MD_PROFILE_RECORDS 16
typedef struct {
    char name[192];
    int tv,full,preset;
    unsigned long long frames,skipped,total[MD_PROFILE_PHASES],peak[MD_PROFILE_PHASES];
    unsigned long long fft_calls,fft_cold_calls,fft_cold_us,fft_total[3],fft_peak[3];
} MdProfileRecord;
static MdProfileRecord md_profiles[MD_PROFILE_RECORDS];
static int md_profile_enabled,md_profile_count,md_profile_current=-1;
static unsigned int md_profile_dropped;
static unsigned long long md_profile_tick,md_profile_sample[MD_PROFILE_PHASES];
static unsigned long long md_fft_tick,md_fft_sample[3];
static void md_profile_fft(int phase,int cold) {
    if(md_profile_current<0)return;
    unsigned long long now=sceKernelGetSystemTimeWide();
    if(phase>0)md_fft_sample[phase-1]=now-md_fft_tick;
    md_fft_tick=now;
    if(phase!=3)return;
    MdProfileRecord *p=&md_profiles[md_profile_current];
    if(cold) {
        p->fft_cold_calls++;
        for(int i=0;i<3;i++)p->fft_cold_us+=md_fft_sample[i];
    } else {
        p->fft_calls++;
        for(int i=0;i<3;i++) {
            p->fft_total[i]+=md_fft_sample[i];
            if(md_fft_sample[i]>p->fft_peak[i])p->fft_peak[i]=md_fft_sample[i];
        }
    }
}
void md_profile_reset(int enabled) {
    md_profile_enabled=enabled;md_profile_count=0;md_profile_current=-1;md_profile_dropped=0;
    memset(md_profiles,0,sizeof(md_profiles));
    md_fft_profile_hook=enabled?md_profile_fft:NULL;
}
void md_profile_select(const char *name,int tv,int full,int preset) {
    if(!md_profile_enabled)return;
    char key[192];snprintf(key,sizeof(key),"%s",name);
    for(int i=0;i<md_profile_count;i++) {
        MdProfileRecord *p=&md_profiles[i];
        if(p->tv==tv && p->full==full && p->preset==preset && !strcmp(p->name,key)) {
            md_profile_current=i;return;
        }
    }
    if(md_profile_count==MD_PROFILE_RECORDS){md_profile_current=-1;md_profile_dropped++;return;}
    md_profile_current=md_profile_count++;
    MdProfileRecord *p=&md_profiles[md_profile_current];
    snprintf(p->name,sizeof(p->name),"%s",key);p->tv=tv;p->full=full;p->preset=preset;
}
static void md_profile_begin(void) {
    if(md_profile_current>=0)md_profile_tick=sceKernelGetSystemTimeWide();
}
static void md_profile_mark(int phase) {
    if(md_profile_current<0)return;
    unsigned long long now=sceKernelGetSystemTimeWide();
    md_profile_sample[phase]=now-md_profile_tick;md_profile_tick=now;
}
static void md_profile_commit(void) {
    if(md_profile_current<0)return;
    MdProfileRecord *p=&md_profiles[md_profile_current];p->frames++;
    for(int i=0;i<MD_PROFILE_PHASES;i++) {
        p->total[i]+=md_profile_sample[i];
        if(md_profile_sample[i]>p->peak[i])p->peak[i]=md_profile_sample[i];
    }
}
/* Read only after playback stops. One record per call; no allocations. */
int md_profile_report(int index,char *text,int size) {
    if(!md_profile_enabled || index<0 || index>=md_profile_count || size<1)return 0;
    static const char *names[]={"setup","frame_shapes","pixel_formulas","audio_fft",
        "custom_wave_eval","geometry_submit","gpu_wait"};
    MdProfileRecord *p=&md_profiles[index];
    int n=snprintf(text,size,"MilkDrop profile: %s | %s %s preset=%d frames=%llu skipped=%llu untracked_calls=%u\n",
        p->name,p->tv?"TV":"LCD",p->full?"full":"window",p->preset,p->frames,p->skipped,md_profile_dropped);
    for(int i=0;i<MD_PROFILE_PHASES && n>=0 && n<size;i++)
        n+=snprintf(text+n,size-n,"  %s avg_us=%llu max_us=%llu\n",names[i],
            p->frames?p->total[i]/p->frames:0,p->peak[i]);
    if(n>=0 && n<size)n+=snprintf(text+n,size-n,
        "  fft_warm_calls=%llu cold_calls=%llu cold_total_us=%llu\n",
        p->fft_calls,p->fft_cold_calls,p->fft_cold_us);
    static const char *fft_names[]={"fft_prepare","fft_transform","fft_magnitude"};
    for(int i=0;i<3 && n>=0 && n<size;i++)
        n+=snprintf(text+n,size-n,"  %s avg_us=%llu max_us=%llu\n",fft_names[i],
            p->fft_calls?p->fft_total[i]/p->fft_calls:0,p->fft_peak[i]);
    return 1;
}
#endif
