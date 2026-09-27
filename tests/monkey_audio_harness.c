#include "monkey_audio.h"
void reset(MonkeyAudio *s){monkey_audio_reset(s);}
int detect(MonkeyAudio *s,int sensitivity,float immediate,float average,float dt){return monkey_audio_detect(s,sensitivity,immediate,average,dt);}
int step(MonkeyAudio *s,const float raw[3],int sensitivity,float dt){return monkey_audio_step(s,raw,sensitivity,dt);}
void bands(const float input[512],float output[3]){monkey_audio_bands(input,output);}
