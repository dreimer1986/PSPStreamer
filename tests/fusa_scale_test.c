#include <assert.h>
#include <stdlib.h>
#include "../psp-fusa-probe/scale.h"
#include "../psp-fusa-probe/snapshot.h"
int main(void)
{
    assert(fs_handoff_valid(1,1,7,7,1));
    assert(!fs_handoff_valid(1,1,7,8,1));
    assert(!fs_handoff_valid(1,1,7,7,0));
    assert(!fs_handoff_valid(1,1,~0U,0,1));
    assert(!fs_handoff_valid(1,0,7,7,1)); /* Timeout/cancellation. */
    assert(!fs_handoff_valid(1,2,7,7,1)); /* Different producer ticket. */
    assert(!fs_handoff_valid(0,0,7,7,1)); /* No owner. */
    assert(fs_next_frame(1000,41000)==84333);
    assert(fs_next_frame(1000,100000)==100000);
    FsSnapshots ring={0};
    assert(fs_snapshot_read(&ring)==-1);
    int a=fs_snapshot_write(&ring),b=fs_snapshot_write(&ring);
    assert(a==0&&b==1&&fs_snapshot_write(&ring)==-1);
    fs_snapshot_publish(&ring,a,1);fs_snapshot_publish(&ring,b,2);
    assert(fs_snapshot_read(&ring)==b&&ring.format[b]==2&&ring.state[a]==FS_FREE);
    assert(fs_snapshot_write(&ring)==a);
    assert(fs_snapshot_write(&ring)==-1); /* Reader may never be overwritten. */
    fs_snapshot_publish(&ring,a,0);
    assert(fs_snapshot_write(&ring)==a); /* Supersede stale queued frame only. */
    fs_snapshot_publish(&ring,a,1);ring.state[b]=FS_FREE;
    assert(fs_snapshot_read(&ring)==a&&ring.format[a]==1);
    assert(fs_source_valid(0x04044000,512,1));
    assert(fs_source_valid(0x44000000,1024,2));
    assert(!fs_source_valid(0x041fff00,512,1));
    assert(!fs_source_valid(0x04200000,512,1));
    assert(!fs_source_valid(0x04000000,512,3));
    assert(!fs_source_valid(0x04000000,768,1));
    assert(!fs_source_valid(0x04000001,512,1));
    for(int stride=512;stride<=1024;stride*=2) {
        uint16_t *src=malloc(stride*272*2),*dst=malloc((768*480+4)*2);
        uint16_t *snap=malloc((480*272+4)*2);
        assert(src&&dst&&snap);
        for(int i=0;i<stride*272;i++)src[i]=(uint16_t)i;
        for(int i=0;i<768*480+4;i++)dst[i]=0xabcd;
        for(int i=0;i<480*272+4;i++)snap[i]=0xcdef;
        fs_copy16(snap+2,src,stride);
        assert(snap[0]==0xcdef&&snap[1]==0xcdef&&snap[480*272+2]==0xcdef&&snap[480*272+3]==0xcdef);
        for(unsigned y=0;y<272;y++)for(unsigned x=0;x<480;x++)
            assert(snap[2+y*480+x]==src[y*stride+x]);
        /* Game buffer reuse after capture must not affect the output. */
        for(int i=0;i<stride*272;i++)src[i]=0;
        fs_scale16(dst+2,snap+2,480);
        assert(dst[0]==0xabcd&&dst[1]==0xabcd&&dst[768*480+2]==0xabcd&&dst[768*480+3]==0xabcd);
        for(unsigned y=0;y<480;y++)for(unsigned x=0;x<768;x++)
            assert(dst[2+y*768+x]==(x<720?(uint16_t)((y*272/480)*stride+x*2/3):0xabcd));
        free(src);free(dst);free(snap);
    }
    return 0;
}
