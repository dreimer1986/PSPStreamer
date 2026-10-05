#include <assert.h>
#include <stdlib.h>
#include "../xbox-client/visual_scale.h"
int main(void){
    uint32_t *src=malloc(768*480*4),*dst=malloc(1936*1082*4);
    assert(src&&dst);
    for(int y=0;y<480;y++)for(int x=0;x<768;x++)src[y*768+x]=(y<<12)^x;
    const int sizes[][4]={{720,480,1920,1080},{506,232,1349,522},{660,385,1760,866},{488,135,488,135},{720,480,640,480}};
    for(unsigned i=0;i<sizeof(sizes)/sizeof(*sizes);i++)for(int swap=0;swap<2;swap++){
        int sw=sizes[i][0],sh=sizes[i][1],w=sizes[i][2],h=sizes[i][3];
        for(int n=0;n<1936*1082;n++)dst[n]=0x12345678;
        assert(xbox_scale_opaque(dst+1936+3,1936,w,h,src,768,sw,sh,swap));
        for(int y=0;y<h;y++)for(int x=0;x<w;x++){
            uint32_t c=src[((2*y+1)*sh/(2*h))*768+(2*x+1)*sw/(2*w)];
            if(swap)c=(c&0xff00ff00)|((c&255)<<16)|((c>>16)&255);
            assert(dst[(y+1)*1936+x+3]==(c|0xff000000));
        }
        assert(dst[0]==0x12345678&&dst[1936+2]==0x12345678);
        assert(dst[1936+w+3]==0x12345678&&dst[(h+1)*1936+3]==0x12345678);
    }
    assert(!xbox_scale_opaque(dst,1936,1921,1080,src,768,720,480,0));
    free(src);free(dst);return 0;
}
