#include <assert.h>
#include <stdio.h>
#include "../psp-client/milkdrop_signal.c"
#include "theme_layout.h"
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
    puts("Spectrum: disabled fast path, DFT reference, bounds, pause/reset and demand gating passed");
}
