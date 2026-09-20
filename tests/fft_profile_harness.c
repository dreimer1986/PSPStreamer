#include <assert.h>
#include <math.h>
#include <string.h>
#include "milkdrop_wave.h"
static int events,cold_calls,expected_cold;
static void profile(int phase,int cold) {
    assert(phase==events%4 && cold==expected_cold);
    events++;
    if(!phase&&cold)cold_calls++;
}
int main(void) {
    short samples[1024]={0};float bins[512],reference[512];
    md_fft_profile_hook=profile;expected_cold=1;
    md_wave_spectrum(samples,bins);
    assert(events==4 && cold_calls==1);
    for(int i=0;i<512;i++)assert(bins[i]==0);
    expected_cold=0;
    for(int i=0;i<1024;i++)samples[i]=(short)(14000*sin(6.283185307179586*37*i/1024));
    md_wave_spectrum(samples,bins);
    assert(events==8 && cold_calls==1);
    assert(bins[37]>.4f && bins[37]<.44f);
    memcpy(reference,bins,sizeof(bins));
    md_fft_profile_hook=0;
    md_wave_spectrum(samples,bins);
    assert(events==8 && !memcmp(reference,bins,sizeof(bins)));
    /* Direct double-precision DFT checks windowing, normalization and bins;
     * this tests numerical output, not PSP performance or VFPU accuracy. */
    for(int k=0;k<512;k++) {
        double real=0,imag=0;
        for(int j=0;j<1024;j++) {
            double value=samples[j]/32768.0*(.5-.5*cos(6.283185307179586*j/1023));
            real+=value*cos(6.283185307179586*k*j/1024);
            imag-=value*sin(6.283185307179586*k*j/1024);
        }
        assert(fabs(bins[k]-sqrt(real*real+imag*imag)/256)<.00002);
    }
    return 0;
}
