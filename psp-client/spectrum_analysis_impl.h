/* SPDX-License-Identifier: GPL-2.0-or-later
 * Audio equations adapted from MilkDrop 2, Copyright 2005-2013 Nullsoft, Inc.
 * Original BSD-3-Clause notice: licenses/MilkDrop2.txt (included in releases).
 * MilkDrop 2: FFT::Init(576,512,-1) and DoCustomSoundAnalysis.
 * -1 is true in the reference's equalizer test. No shell pre-damping here.
 * UI-owned fixed scratch; no allocation or FFT in the audio worker. */
#include "spectrum_analysis.h"
#include "monkey_audio.h"
int spectrum_analysis_mode=0,spectrum_band_count=32,spectrum_tv_band_count=32;
int spectrum_gain_db=0;
static int spectrum_output_tv;
static volatile int spectrum_capture;
static volatile unsigned int spectrum_sequence;
static volatile short spectrum_pcm[576];
static short spectrum_copy[576];
static unsigned int spectrum_consumed;
static float spectrum_real[1024],spectrum_imag[1024],spectrum_window[576],spectrum_equalize[512];
static float spectrum_bins[512],spectrum_raw[3],spectrum_average[3],spectrum_history[3],spectrum_rel[6];
static float spectrum_amplitude[512];
static unsigned char spectrum_bars[64];
static unsigned long long spectrum_tick;
static unsigned int spectrum_frames;
static int spectrum_tables,spectrum_active;
static int spectrum_kind,spectrum_monkey_ready;
static float spectrum_monkey[3];
const float *spectrum_monkey_raw(void){return spectrum_monkey_ready?spectrum_monkey:NULL;}
int spectrum_band_valid(int n){return n==12||n==24||n==32||n==48||n==64;}
int spectrum_band_choice(int n,int direction){
    const int choices[]={12,24,32,48,64};int i=0;
    while(i<4 && choices[i]<n)i++;
    i=(i+(direction<0?4:1))%5;return choices[i];
}
void spectrum_analysis_output(int tv){
    if(spectrum_output_tv!=!!tv){spectrum_output_tv=!!tv;spectrum_tick=0;}
}
int spectrum_bar_count(void){return spectrum_analysis_mode?(spectrum_output_tv?spectrum_tv_band_count:spectrum_band_count):12;}
int spectrum_bar_level(int i,int legacy){return spectrum_analysis_mode?spectrum_bars[i]:legacy;}
const float *spectrum_relative(void){return spectrum_rel;}
void spectrum_pcm_publish(const short *pcm,int frames){
    if(!spectrum_capture || frames<=0)return;
    spectrum_sequence++;__sync_synchronize();
    for(int i=0;i<576;i++)spectrum_pcm[i]=i<frames?pcm[i*2]:0;
    __sync_synchronize();spectrum_sequence++;
}
void spectrum_analysis_reset(void){
    spectrum_capture=0;spectrum_active=0;spectrum_tick=0;spectrum_frames=0;
    spectrum_kind=0;spectrum_monkey_ready=0;
    spectrum_consumed=spectrum_sequence;
    memset(spectrum_bins,0,sizeof(spectrum_bins));memset(spectrum_raw,0,sizeof(spectrum_raw));
    memset(spectrum_average,0,sizeof(spectrum_average));memset(spectrum_history,0,sizeof(spectrum_history));
    memset(spectrum_rel,0,sizeof(spectrum_rel));memset(spectrum_bars,0,sizeof(spectrum_bars));
    memset(spectrum_amplitude,0,sizeof(spectrum_amplitude));
}
static void spectrum_transform(void){
    if(!spectrum_tables){
        for(int i=0;i<576;i++)spectrum_window[i]=.5f+.5f*sinf(i*(6.2831853f/576)-1.5707963268f);
        for(int i=0;i<512;i++)spectrum_equalize[i]=-.02f*logf((512-i)/512.f);
        spectrum_tables=1;
    }
    int previous=0;
    for(int i=0;i<1024;i++){
        /* Winamp's signed 8-bit waveform range, from our decoded 16-bit PCM. */
        int sample=i<576?(int)floorf(spectrum_copy[i]/256.f):0;
        float input=sample;
        if(spectrum_kind==2)input=.5f*(sample+(i?previous:sample));
        previous=sample;
        spectrum_real[i]=i<576?input*spectrum_window[i]:0;spectrum_imag[i]=0;
    }
    for(int i=1,j=0;i<1024;i++){
        int bit=512;for(;j&bit;bit>>=1)j^=bit;j^=bit;
        if(i<j){float t=spectrum_real[i];spectrum_real[i]=spectrum_real[j];spectrum_real[j]=t;}
    }
    for(int size=2;size<=1024;size*=2){
        float cr=cosf(-6.283185307f/size),ci=sinf(-6.283185307f/size),wr=1,wi=0;
        for(int m=0;m<size/2;m++){
            for(int i=m;i<1024;i+=size){
                int j=i+size/2;float re=wr*spectrum_real[j]-wi*spectrum_imag[j],im=wr*spectrum_imag[j]+wi*spectrum_real[j];
                spectrum_real[j]=spectrum_real[i]-re;spectrum_imag[j]=spectrum_imag[i]-im;
                spectrum_real[i]+=re;spectrum_imag[i]+=im;
            }
            float old=wr;wr=wr*cr-wi*ci;wi=wi*cr+old*ci;
        }
    }
    for(int i=0;i<512;i++){
        float magnitude=sqrtf(spectrum_real[i]*spectrum_real[i]+spectrum_imag[i]*spectrum_imag[i]);
        spectrum_bins[i]=spectrum_equalize[i]*magnitude;
        /* Display: remove frequency weighting, normalize the Hann coherent
         * gain (576/2) and full-scale signed 8-bit sine amplitude (128).
         * A real sine splits equally into positive/negative frequencies. */
        spectrum_amplitude[i]=i?magnitude*(2.f/(128.f*288.f)):0;
    }
    for(int g=0;g<3;g++){
        spectrum_raw[g]=0;
        for(int i=512*g/6;i<512*(g+1)/6;i++)spectrum_raw[g]+=spectrum_bins[i];
    }
    if(spectrum_kind==2){monkey_audio_bands(spectrum_bins,spectrum_monkey);spectrum_monkey_ready=1;}
}
void spectrum_analysis_step(unsigned long long now,int needed,int playing){
    int active=spectrum_analysis_mode && needed;
    if(!active){if(spectrum_active)spectrum_analysis_reset();return;}
    if(spectrum_kind!=needed){spectrum_analysis_reset();spectrum_kind=needed;}
    spectrum_active=1;spectrum_capture=playing;
    if(!playing)spectrum_monkey_ready=0;
    if(spectrum_tick && now>=spectrum_tick && now-spectrum_tick<50000)return;
    float steps=spectrum_tick && now>=spectrum_tick?(now-spectrum_tick)*.000001f*30:1;
    spectrum_tick=now;
    unsigned int seq=spectrum_sequence;
    if(playing && seq && !(seq&1) && seq!=spectrum_consumed){
        __sync_synchronize();for(int i=0;i<576;i++)spectrum_copy[i]=spectrum_pcm[i];__sync_synchronize();
        if(seq==spectrum_sequence){spectrum_consumed=seq;spectrum_transform();}
    }
    if(!playing){memset(spectrum_raw,0,sizeof(spectrum_raw));memset(spectrum_bins,0,sizeof(spectrum_bins));memset(spectrum_amplitude,0,sizeof(spectrum_amplitude));}
    if(spectrum_kind==2)return; /* Monkey owns its per-render history/detector. */
    for(int g=0;g<3;g++){
        float raw=spectrum_raw[g],rate=powf(raw>spectrum_average[g]?.2f:.5f,steps);
        spectrum_average[g]=spectrum_average[g]*rate+raw*(1-rate);
        rate=powf(spectrum_frames<50?.9f:.992f,steps);
        spectrum_history[g]=spectrum_history[g]*rate+raw*(1-rate);
        spectrum_rel[g]=fabsf(spectrum_history[g])<.001f?1:raw/spectrum_history[g];
        spectrum_rel[g+3]=fabsf(spectrum_history[g])<.001f?1:spectrum_average[g]/spectrum_history[g];
    }
    if(spectrum_frames<50)spectrum_frames++;
    /* Display-only grouping: linear bins avoid duplicating narrow bass bins.
     * Never feed display gain, clipping or chosen bar count into MilkDrop. */
    int count=spectrum_bar_count();
    for(int b=0;b<count;b++){
        float peak=0;for(int i=512*b/count;i<512*(b+1)/count;i++)if(spectrum_amplitude[i]>peak)peak=spectrum_amplitude[i];
        /* Fixed -60..0 dBFS display span; no automatic gain riding. */
        float value=peak>0?(20*log10f(peak)+spectrum_gain_db+60)*(100.f/60):0;
        spectrum_bars[b]=(unsigned char)(fminf(100,fmaxf(0,value))+.5f);
    }
}
