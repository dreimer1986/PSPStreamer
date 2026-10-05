/* PSP's original 50 ms attack/decay steps, independent of Xbox redraw rate. */
#ifndef XBOX_VISUAL_ENVELOPE_H
#define XBOX_VISUAL_ENVELOPE_H
static unsigned xbox_envelope_steps(unsigned now,unsigned *last,int reset){
    if(reset){*last=now;return 1;}
    unsigned elapsed=now-*last;
    if(elapsed>1000){elapsed=1000;*last=now-1000;}
    unsigned steps=elapsed/50;*last+=steps*50;return steps;
}
static int xbox_envelope(int value,int target,unsigned steps){
    while(steps--)value=target>value?value+(target-value+1)/2:value>3?value-3:0;
    return value;
}
#endif
