#ifndef CAVE_RUMBLE_H
#define CAVE_RUMBLE_H
#include <math.h>
/* Independent gains; small motor is binary, reserved for strong impacts. */
static inline void cave_rumble_mix(float bass,float beat,float event,int music,int game,int playing,unsigned *small,unsigned *large) {
    *small=*large=0;if(!playing)return;
    float m=fminf(1,fmaxf(0,bass*.65f+beat*.6f))*music*.01f;
    float g=fminf(1,fmaxf(0,event))*game*.01f;
    float level=fminf(1,fmaxf(m,g));
    *large=(unsigned)(level*255+.5f);*small=g>.8f;
}
#endif
