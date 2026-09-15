#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
typedef uint32_t u32;
#define SPECTRUM_BANDS 12
static int spectrum_display[SPECTRUM_BANDS];
static int music_ui_envelope(int displayed,int target) {
    return target>displayed?displayed+(target-displayed+1)/2:displayed>3?displayed-3:0;
}
#include "spectrum_fullscreen.h"
int main(void) {
    int tv,i,x,y;
    for(tv=0;tv<2;tv++) {
        int w=tv?720:480,h=tv?480:272,stride=tv?768:512;
        size_t count=(size_t)stride*h;
        u32 *memory=malloc((count+2)*4),*pixels=memory+1;
        unsigned char levels[SPECTRUM_BANDS];
        for(i=0;i<(int)count+2;i++) memory[i]=0xDEADBEEF;
        memset(levels,100,sizeof(levels)); memset(spectrum_display,0,sizeof(spectrum_display));
        spectrum_fullscreen_reset();
        for(i=0;i<7;i++) spectrum_fullscreen_render(pixels,w,h,stride,levels,1);
        assert(pixels[(h/30)*stride+w/30]!=0x00080E14);
        assert(pixels[(h-h/30-1)*stride+w/30]!=0x00080E14);
        for(i=0;i<40;i++) spectrum_fullscreen_render(pixels,w,h,stride,levels,0);
        for(y=0;y<h;y++) {
            for(x=0;x<w;x++) assert(pixels[y*stride+x]==0x00080E14);
            for(x=w;x<stride;x++) assert(pixels[y*stride+x]==0xDEADBEEF);
        }
        assert(memory[0]==0xDEADBEEF && memory[count+1]==0xDEADBEEF);
        free(memory);
    }
    return 0;
}
