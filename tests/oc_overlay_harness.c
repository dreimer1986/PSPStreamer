#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "overlay_pixels.h"
static OcOverlay overlay;
int main(void) {
    char lines[3][40]={{0}};
    strcpy(lines[0],"OC CPU 382.9 BUS 382.9 MHZ EST");
    strcpy(lines[1],"TARGET 383 SONY 333 MHZ");
    strcpy(lines[2],"ACTIVE ENFORCE ON CB OK");
    assert(oc_osd_layout(0x44000000,2*1024*1024,480,272,512,3));
    assert(oc_osd_layout(0x44000000,2*1024*1024,720,480,768,3));
    assert(!oc_osd_layout(0x441f0000,2*1024*1024,720,480,768,3));
    assert(!oc_osd_layout(0x08800000,2*1024*1024,480,272,512,3));
    assert(!oc_osd_layout(0x44000000,2*1024*1024,480,272,256,3));
    assert(!oc_osd_layout(0x44000000,2*1024*1024,480,272,512,4));
    for(int mode=0;mode<4;mode++) {
        int bytes=mode==3?4:2,size=512*272*bytes;
        unsigned char *image=malloc(size);assert(image);memset(image,0x12,size);
        oc_osd_draw(&overlay,image,512,mode,lines);
        assert(image[0]==0x12 && image[size-1]==0x12);
        oc_osd_set(image,8*512+8,mode,0x1234);
        oc_osd_restore(&overlay);
        assert(oc_osd_get(image,8*512+8,mode)==0x1234);
        oc_osd_set(image,8*512+8,mode,mode==3?0x12121212:0x1212);
        for(int i=0;i<size;i++)assert(image[i]==0x12);
        oc_osd_restore(&overlay);
        free(image);
    }
    return 0;
}
