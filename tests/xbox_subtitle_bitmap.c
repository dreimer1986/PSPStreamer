#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../xbox-client/subtitle_bitmap.h"
int main(void){
    XboxBitmapCue *cues=calloc(XBOX_BITMAP_CUES,sizeof(*cues));assert(cues);
    assert(xbox_bitmap_parse("{\"t\":\"pgs\",\"c\":[[0,100,1,0,1,1,4,2]]}",cues)==1);
    assert(xbox_bitmap_parse("{\"t\":\"pgs\",\"c\":[[0,100,1,0,999999,1,4,2]]}",cues)<0);
    assert(xbox_bitmap_parse("{\"t\":\"pgs\",\"c\":[[0,100,0,0,1920,1080,1920,1080]]}",cues)<0);
    unsigned char sprite[1025]={0};sprite[4]=sprite[5]=sprite[6]=sprite[7]=255;sprite[1024]=1;
    XboxBitmapSlot slot={{0,100,1,0,1,1,4,2},sprite};
    unsigned char data[20];memset(data,0xaa,sizeof(data));
    for(int i=2;i<18;i+=2){data[i]=16;data[i+1]=128;}
    xbox_bitmap_blend(data+2,8,4,2,&slot);
    assert(data[0]==0xaa&&data[1]==0xaa&&data[18]==0xaa&&data[19]==0xaa);
    assert(data[2]==16&&data[4]==235&&data[6]==16&&data[10]==16);
    sprite[7]=0;unsigned char copy[20];memcpy(copy,data,sizeof(data));
    xbox_bitmap_blend(data+2,8,4,2,&slot);assert(!memcmp(copy,data,sizeof(data)));
    sprite[7]=128;data[4]=16;xbox_bitmap_blend(data+2,8,4,2,&slot);assert(data[4]==126);
    for(unsigned h=480;h<=1080;h+=120){
        unsigned w=1920,pitch=w*2;unsigned char *out=calloc(pitch,h);assert(out);
        slot.cue=(XboxBitmapCue){0,100,3,1,1,1,4,2};
        xbox_bitmap_blend(out,pitch,w,h,&slot);free(out);
    }
    free(cues);puts("PGS: metadata limits, clipping, odd-X chroma, transparency, SD/HD bounds passed");
}
