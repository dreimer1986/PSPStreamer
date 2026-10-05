/* Opaque nearest-neighbour presentation. Resample each source row once, then
 * reuse it for repeated destination rows. In particular, do not repeatedly read
 * write-combined GPU memory for every enlarged HD pixel. No SSE2 required. */
#ifndef XBOX_VISUAL_SCALE_H
#define XBOX_VISUAL_SCALE_H
#include <stdint.h>
#include <string.h>
#include "visual_copy.h"
static int xbox_scale_opaque(uint32_t *dst,int pitch,int dw,int dh,
                             const uint32_t *src,int stride,int sw,int sh,int abgr){
    if(!dst||!src||sw<1||sh<1||sw>720||dw<1||dw>1920||dh<1||dh>1080||pitch<dw||stride<sw)return 0;
    uint32_t input[720],row[1920];int columns[1920],previous=-1;
    for(int x=0;x<dw;x++)columns[x]=(int)(((int64_t)(2*x+1)*sw)/(2*dw));
    for(int y=0;y<dh;y++){
        int sy=(int)(((int64_t)(2*y+1)*sh)/(2*dh));
        if(sy!=previous){
            xbox_visual_copy(input,src+sy*stride,sw*4);
            for(int x=0;x<dw;x++){
                uint32_t c=input[columns[x]];
                if(abgr)c=(c&0xff00ff00)|((c&255)<<16)|((c>>16)&255);
                row[x]=c|0xff000000;
            }
            previous=sy;
        }
        xbox_visual_copy(dst+y*pitch,row,dw*4);
    }
    return 1;
}
#endif
