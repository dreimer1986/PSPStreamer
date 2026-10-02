#include <assert.h>
#include <stdlib.h>
#include "../psp-fusa-probe/scale.h"
int main(void)
{
    assert(fs_source_valid(0x04044000,512,1));
    assert(fs_source_valid(0x44000000,1024,2));
    assert(!fs_source_valid(0x041fff00,512,1));
    assert(!fs_source_valid(0x04200000,512,1));
    assert(!fs_source_valid(0x04000000,512,3));
    assert(!fs_source_valid(0x04000000,768,1));
    assert(!fs_source_valid(0x04000001,512,1));
    for(int stride=512;stride<=1024;stride*=2) {
        uint16_t *src=malloc(stride*272*2),*dst=malloc((768*480+2)*2);
        assert(src&&dst);
        for(int i=0;i<stride*272;i++)src[i]=(uint16_t)i;
        for(int i=0;i<768*480+2;i++)dst[i]=0xabcd;
        fs_scale16(dst+1,src,stride);
        assert(dst[0]==0xabcd&&dst[768*480+1]==0xabcd);
        for(unsigned y=0;y<480;y++)for(unsigned x=0;x<768;x++)
            assert(dst[1+y*768+x]==(x<720?src[(y*272/480)*stride+x*2/3]:0xabcd));
        free(src);free(dst);
    }
    return 0;
}
