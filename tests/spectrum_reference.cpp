#include <cassert>
#include <cmath>
#include <cstdio>
#include "fft.h"
extern "C" {
#include "spectrum_analysis.h"
#include "milkdrop_signal.h"
}
int main(){
    FFT reference;reference.Init(576,512,-1);
    float average[3]={},history[3]={};unsigned char legacy[12]={};
    spectrum_analysis_mode=1;spectrum_analysis_reset();
    unsigned long long now=1000000;spectrum_analysis_step(now,1,1);
    short pcm[1152];float input[576],bins[512];
    for(int frame=1;frame<90;frame++){
        for(int i=0;i<576;i++){
            pcm[i*2]=(short)((frame%11==0?0:12000)*sin(i*.03+frame*.4)+(frame%7)*1200*cos(i*.64));
            pcm[i*2+1]=(short)-pcm[i*2];input[i]=floorf(pcm[i*2]/256.f);
        }
        reference.time_to_frequency_domain(input,bins);
        spectrum_pcm_publish(pcm,576);spectrum_analysis_step(now+=50000,1,1);
        MdSignalState s={};md_signal_update(&s,legacy,0,now);
        for(int g=0;g<3;g++){
            float raw=0;for(int b=512*g/6;b<512*(g+1)/6;b++)raw+=bins[b];
            float rate=powf(raw>average[g]?.2f:.5f,1.5f);
            average[g]=average[g]*rate+raw*(1-rate);
            rate=powf(frame<50?.9f:.992f,1.5f);history[g]=history[g]*rate+raw*(1-rate);
            float immediate=fabsf(history[g])<.001f?1:raw/history[g];
            float attenuated=fabsf(history[g])<.001f?1:average[g]/history[g];
            assert(fabsf(s.signal.values[7+g]-immediate)<.0002f);
            assert(fabsf(s.signal.values[10+g]-attenuated)<.0002f);
        }
    }
    puts("MilkDrop original FFT + DoCustomSoundAnalysis temporal equations: matched");
}
