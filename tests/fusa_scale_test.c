#include <assert.h>
#include <stdlib.h>
#include "../psp-fusa-probe/scale.h"
#include "../psp-fusa-probe/snapshot.h"
int main(void)
{
    assert(fs_ram_source_valid(0xabbbc000,512,1,0x0b800000,0x0bc00000));
    assert(fs_ram_source_valid(0x8bbbc000,512,1,0x0b800000,0x0bc00000));
    assert(!fs_ram_source_valid(0xabbbc000,512,3,0x0b800000,0x0bc00000)); /* Crosses end. */
    assert(!fs_ram_source_valid(0xabbbc001,512,1,0x0b800000,0x0bc00000));
    assert(!fs_ram_source_valid(0xabbbc000,-512,1,0x0b800000,0x0bc00000));
    assert(!fs_ram_source_valid(0xabbbc000,512,1,0,0));
    assert(!fs_ram_source_valid(0xabbbc000,512,1,0x0b800000,0xffffffff));
    assert(fs_source_alias(0xabbbc000)==0xabbbc000);
    assert(fs_source_alias(0x8bbbc000)==0xabbbc000);
    assert(fs_source_alias(0x04000000)==0x44000000);
    assert(fs_next_frame(1000,21000)==34334);
    assert(fs_next_frame(1000,34334)==34334);
    assert(fs_next_frame(1000,41000)==41000);
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
    assert(fs_source_valid(0x04154000,512,3)); /* Gran Turismo HOME buffer. */
    assert(!fs_source_valid(FS_OUTPUT_BASE,512,1));
    assert(!fs_source_valid(0x04200000,512,3)); /* Overlaps output. */
    assert(fs_source_valid(0x04000000,512,3));
    assert(FS_OUTPUT_BASE+2*FS_OUTPUT_BYTES<=0x04400000U);
    assert(fs_blank_source(0x04000000,0,3,1)); /* Soul Calibur HOME. */
    assert(fs_blank_source(0,512,3,1));
    assert(!fs_blank_source(0x04000000,512,3,1));
    assert(!fs_blank_source(0,0,4,1));
    assert(fs_output_format(3)==0&&fs_output_format(1)==1);
    uint32_t *rgba=calloc(512*272,sizeof(*rgba));
    uint16_t *rgb=malloc((480*272+4)*sizeof(*rgb));
    assert(rgba&&rgb);rgb[0]=rgb[1]=rgb[480*272+2]=rgb[480*272+3]=0xdead;
    rgba[0]=0xff0000ff;rgba[1]=0xff00ff00;rgba[2]=0xffff0000;
    rgba[271*512+479]=0xffffffff;
    fs_copy32(rgb+2,rgba,512);
    assert(rgb[2]==0x001f&&rgb[3]==0x07e0&&rgb[4]==0xf800);
    assert(rgb[480*272+1]==0xffff&&rgb[0]==0xdead&&rgb[1]==0xdead&&rgb[480*272+2]==0xdead&&rgb[480*272+3]==0xdead);
    for(unsigned i=0;i<512*272;i++)rgba[i]=i*2654435761U;
    fs_copy32(rgb+2,rgba,512);
    for(unsigned y=0;y<272;y++)for(unsigned x=0;x<480;x++) {
        unsigned p=rgba[y*512+x];
        unsigned reference=((p&0xf8U)>>3)|((p&0xfc00U)>>5)|((p&0xf80000U)>>8);
        assert(rgb[2+y*480+x]==reference);
    }
    free(rgba);free(rgb);
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
