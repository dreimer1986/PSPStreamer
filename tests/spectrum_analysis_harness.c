#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include "../psp-client/milkdrop_signal.c"
#include "theme_layout.h"
static int tone(float frequency,float amplitude,int gain) {
    short pcm[1152];
    for(int i=0;i<576;i++)pcm[2*i]=pcm[2*i+1]=(short)(32767*amplitude*sinf(i*6.283185307f*frequency/44100));
    spectrum_gain_db=gain;spectrum_analysis_reset();
    spectrum_analysis_step(2000000,1,1);spectrum_pcm_publish(pcm,576);
    spectrum_analysis_step(2050000,1,1);
    int peak=0;for(int i=0;i<spectrum_bar_count();i++)if(spectrum_bars[i]>peak)peak=spectrum_bars[i];
    return peak;
}
int main(void){
    short pcm[1152];for(int i=0;i<1152;i++)pcm[i]=(short)(i*37);
    spectrum_analysis_reset();spectrum_analysis_mode=0;
    spectrum_analysis_step(1000000,1,1);spectrum_pcm_publish(pcm,576);
    assert(!spectrum_capture && !spectrum_sequence && !spectrum_tables);
    spectrum_analysis_mode=1;spectrum_analysis_step(1000000,1,1);
    spectrum_pcm_publish(pcm,576);spectrum_analysis_step(1050000,1,1);
    assert(spectrum_tables && spectrum_consumed==spectrum_sequence);
    unsigned seq=spectrum_consumed;float previous=spectrum_rel[0];
    spectrum_analysis_step(1050010,1,1);assert(spectrum_consumed==seq && spectrum_rel[0]==previous);
    /* Independent slow DFT checks the exact 576-window, zero padding and EQ. */
    for(int bin=0;bin<512;bin+=7){
        double re=0,im=0;
        for(int i=0;i<576;i++){
            double v=floor(pcm[i*2]/256.0)*spectrum_window[i],phase=-6.283185307179586*bin*i/1024;
            re+=v*cos(phase);im+=v*sin(phase);
        }
        double expected=spectrum_equalize[bin]*sqrt(re*re+im*im);
        /* Single-precision twiddle recurrence differs slightly from double DFT
         * near Nyquist; the separate original-FFT test checks source parity. */
        assert(fabs(spectrum_bins[bin]-expected)<.005+expected*.0001);
    }
    const int choices[]={12,24,32,48,64};
    for(int k=0;k<5;k++){
        spectrum_band_count=choices[k];assert(spectrum_band_valid(choices[k]));
        for(int tv=0;tv<2;tv++)for(int full=0;full<2;full++)for(int i=0;i<choices[k];i++){
            int x=theme_analyzer_x(tv,full,i,choices[k]),w=theme_analyzer_width(tv,full,choices[k]);
            assert(w>0 && x>=0 && x+w<=(full?(tv?720:480):(tv?TV_LEFT_R:LCD_LEFT_R)));
        }
    }
    spectrum_analysis_step(1100000,1,0);
    for(int i=0;i<64;i++)assert(spectrum_bars[i]==0);
    spectrum_band_count=24;spectrum_tv_band_count=64;
    spectrum_analysis_output(0);assert(spectrum_bar_count()==24);
    spectrum_analysis_output(1);assert(spectrum_bar_count()==64);
    spectrum_analysis_output(0);assert(spectrum_bar_count()==24);
    spectrum_analysis_step(1150000,0,1);assert(!spectrum_capture);
    spectrum_analysis_mode=0;assert(spectrum_bar_count()==12 && spectrum_bar_level(0,73)==73);
    spectrum_analysis_mode=1;spectrum_band_count=64;spectrum_analysis_reset();
    assert(spectrum_rel[0]==0 && spectrum_consumed==spectrum_sequence);
    const float frequencies[]={100,440,1000,4000,10000};
    for(int i=0;i<5;i++){int peak=tone(frequencies[i],1,0);assert(peak>=97 && peak<=100);}
    int quiet=tone(1000,.1f,0);assert(quiet>=64 && quiet<=67);
    float original[512];memcpy(original,spectrum_bins,sizeof(original));
    int boosted=tone(1000,.1f,12);assert(abs(boosted-quiet-20)<=1);
    assert(!memcmp(original,spectrum_bins,sizeof(original)));
    assert(tone(1000,0,24)==0);
    spectrum_analysis_mode=0;assert(spectrum_bar_level(0,73)==73);
    spectrum_analysis_mode=1;
    spectrum_analysis_step(3000000,2,1);spectrum_pcm_publish(pcm,576);spectrum_analysis_step(3050000,2,1);
    assert(spectrum_monkey_raw());
    for(int bin=0;bin<512;bin+=13) {
        double re=0,im=0;
        for(int i=0;i<576;i++) {
            double v=.5*(floor(pcm[2*i]/256.0)+floor(pcm[2*(i?i-1:0)]/256.0))*spectrum_window[i];
            double phase=-6.283185307179586*bin*i/1024;
            re+=v*cos(phase);im+=v*sin(phase);
        }
        double expected=spectrum_equalize[bin]*sqrt(re*re+im*im);
        assert(fabs(spectrum_bins[bin]-expected)<.005+expected*.0001);
    }
    for(int g=0;g<3;g++)assert(isfinite(spectrum_monkey_raw()[g]));
    spectrum_analysis_step(3100000,2,0);assert(!spectrum_monkey_raw());
    spectrum_analysis_step(3150000,1,1);assert(!spectrum_monkey_raw());
    puts("Spectrum: disabled fast path, DFT reference, bounds, pause/reset and demand gating passed");
}
