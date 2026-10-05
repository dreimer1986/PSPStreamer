#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include "../xbox-client/visual_envelope.h"
static int run(unsigned interval){
    unsigned last=0;int value=100;
    for(unsigned now=interval;now<1000;now+=interval)value=xbox_envelope(value,0,xbox_envelope_steps(now,&last,0));
    return xbox_envelope(value,0,xbox_envelope_steps(1000,&last,0));
}
int main(void){
    assert(run(25)==40&&run(50)==40&&run(100)==40&&run(143)==40);
    assert(xbox_envelope(0,100,2)==75);
    assert(xbox_envelope(99,100,1)==100);
    assert(xbox_envelope(100,100,1)==97); /* PSP equality policy retained. */
    unsigned last=UINT_MAX-49;assert(xbox_envelope_steps(50,&last,0)==2);
    assert(xbox_envelope_steps(99,&last,0)==0);
    assert(xbox_envelope_steps(100,&last,0)==1);
    assert(xbox_envelope_steps(10000,&last,0)==20);
    assert(xbox_envelope_steps(1,&last,1)==1&&last==1);
    puts("Spectrum: PSP attack/decay at 7, 10, 20 and 40 redraws/s, timer wrap and bounded stalls passed");
}
