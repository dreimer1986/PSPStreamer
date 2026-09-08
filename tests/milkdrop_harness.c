#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <sys/mman.h>
#include "milkdrop_warp.h"
enum { GU_TEXTURE_32BITF=1, GU_COLOR_8888=2, GU_VERTEX_32BITF=4, GU_TRANSFORM_2D=8,
       GU_SYNC_FINISH=20, GU_SYNC_WHAT_DONE, GU_DIRECT, GU_DEPTH_TEST, GU_CULL_FACE,
       GU_LIGHTING, GU_BLEND, GU_ALPHA_TEST, GU_STENCIL_TEST, GU_SCISSOR_TEST,
       GU_TEXTURE_2D, GU_PSM_8888, GU_TFX_MODULATE, GU_TCC_RGBA, GU_LINEAR,
       GU_REPEAT, GU_CLAMP, GU_TRIANGLES, GU_LINE_STRIP, GU_SPRITES };
static int gu_live, starts, syncs, target_offset, target_width, target_height, stride;
static int fail_init, fail_start;
static int edram_size = 2*1024*1024, mesh_calls, ring_calls, sprite_calls;
static unsigned char *list_base;
static size_t list_used;
static unsigned long long test_time = 1000000;
static unsigned long long render_cost;
static uint32_t expected_ring_color;
static int expected_left, expected_top, expected_width, expected_height, covered_width;
static unsigned int sceGeEdramGetSize(void) { return edram_size; }
static unsigned long long sceKernelGetSystemTimeWide(void) { return test_time; }
static int sceGuInit(void) {
    assert(!gu_live); if(fail_init) return -1;
    gu_live=1; return 0;
}
static void sceGuTerm(void) { assert(gu_live); gu_live=0; }
static int sceGuStart(int mode, void *list) {
    if(fail_start) return -1;
    assert(gu_live && mode==GU_DIRECT); starts++; list_base=list; list_used=0;
    covered_width=0;
    return 0;
}
static void sceGuSync(int a,int b) { assert(a==GU_SYNC_FINISH && b==GU_SYNC_WHAT_DONE); syncs++; }
static void sceGuFinish(void) { test_time += render_cost; }
static void sceGuDrawBufferList(int format,void *offset,int width) {
    assert(format==GU_PSM_8888);
    target_offset=(int)(uintptr_t)offset; stride=width;
    assert(target_offset==0 || target_offset==1474560 || target_offset==1736704);
}
static void sceGuOffset(int x,int y) { (void)x; (void)y; }
static void sceGuViewport(int x,int y,int w,int h) {
    assert(x==2048 && y==2048); target_width=w; target_height=h;
    assert(target_offset + stride*h*4 <= edram_size);
}
static void sceGuScissor(int x,int y,int w,int h) {
    assert(!x && !y && w==target_width && h==target_height);
}
static void sceGuDisable(int what) { (void)what; }
static void sceGuEnable(int what) { (void)what; }
static void sceGuTexMode(int p,int a,int b,int c) { assert(p==GU_PSM_8888 && !a && !b && !c); }
static void sceGuTexImage(int level,int w,int h,int s,const void *texture) {
    uintptr_t offset=(uintptr_t)texture-0x04000000;
    assert(!level && w==256 && h==256 && s==256);
    assert(offset==1474560 || offset==1736704);
    assert(offset!=(uintptr_t)target_offset);
}
static void sceGuTexFunc(int a,int b) { (void)a; (void)b; }
static void sceGuTexFilter(int a,int b) { (void)a; (void)b; }
static void sceGuTexWrap(int a,int b) { (void)a; (void)b; }
static void sceGuTexScale(float a,float b) { (void)a; (void)b; }
static void sceGuTexOffset(float a,float b) { (void)a; (void)b; }
static void sceGuTexFlush(void) {}
static void sceGuTexSync(void) {}
static void *sceGuGetMemory(int bytes) {
    void *result=list_base+list_used;
    list_used+=(bytes+15)&~15;
    assert(list_used<32768); /* at least half the command list stays free */
    return result;
}
static void sceGuDrawArray(int type,int format,int count,const void *indices,const void *data) {
    const MdVertex *v=data;
    assert(format==15 && !indices);
    if(type==GU_TRIANGLES) { assert(count==MD_MESH_VERTICES); mesh_calls++; }
    else if(type==GU_LINE_STRIP) {
        assert(count==97); ring_calls++;
        if(expected_ring_color) for(int i=0;i<count;i++) assert(v[i].color==expected_ring_color);
    }
    else {
        assert(type==GU_SPRITES && count==2 && !target_offset); sprite_calls++;
        assert(v[0].x==expected_left+covered_width && v[0].y==expected_top);
        assert(v[1].y==expected_top+expected_height);
        covered_width+=(int)(v[1].x-v[0].x);
        assert(covered_width<=expected_width);
    }
    for(int i=0;i<count;i++) {
        assert(isfinite(v[i].u) && isfinite(v[i].v));
        assert(v[i].x>=0 && v[i].x<=target_width);
        assert(v[i].y>=0 && v[i].y<=target_height);
    }
}
/* GU_ADAPTER */
int main(void) {
    MdVertex mesh[MD_MESH_VERTICES], ring[97];
    MdPreset identity={1,0,0,1,1,1};
    unsigned char bands[12];
    unsigned char *vram=(void *)0x44000000;
    assert(mmap(vram,edram_size,PROT_READ|PROT_WRITE,
                MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0)==vram);
    memset(vram,0xa5,edram_size);
    md_warp_mesh(mesh,&identity,0);
    for(int i=0;i<MD_MESH_VERTICES;i++) {
        assert(fabsf(mesh[i].u-mesh[i].x-.5f)<.0001f);
        assert(fabsf(mesh[i].v-mesh[i].y-.5f)<.0001f);
    }
    edram_size=1024*1024;
    assert(!md_start() && !gu_live);
    edram_size=2*1024*1024;
    fail_init=1; assert(!md_start() && !md_list && !gu_live); fail_init=0;
    assert(md_start()); fail_start=1;
    memset(bands,0,sizeof(bands));
    assert(!md_frame(0,0,bands,0,test_time,0) && !md_list && !gu_live);
    fail_start=0;
    md_custom_preset.warp=md_presets[0];
    md_custom_preset.red=1; md_custom_preset.green=.5f; md_custom_preset.blue=0;
    for(int tv=0;tv<2;tv++) for(int full=0;full<2;full++) {
        expected_left=full ? 0 : tv ? 26 : 38;
        expected_top=full ? 0 : tv ? 86 : 74;
        expected_width=full ? (tv ? 720 : 480) : tv ? 508 : 306;
        expected_height=full ? (tv ? 480 : 272) : tv ? 208 : 75;
        assert(md_start() && md_start());
        for(int i=0;i<1474560;i++) assert(vram[i]==0xa5);
        for(int preset=0;preset<4;preset++) {
            expected_ring_color=preset==3 ? 0xff007fffU : 0;
            for(int frame=0;frame<120;frame++) {
                int calls=starts;
                for(int i=0;i<12;i++) bands[i]=(frame*7+i*9)%101;
                test_time+=100000;
                md_frame(tv,full,bands,frame%101,test_time,preset);
                assert(starts==calls+1);
                assert(covered_width==expected_width);
                md_frame(tv,full,bands,50,test_time,preset);
                assert(starts==calls+1);
                md_audio_ring(ring,bands,frame%101,frame*.1f,preset);
                assert(ring[0].x==ring[96].x && ring[0].y==ring[96].y);
            }
        }
        md_stop(); md_stop(); assert(!gu_live && !md_list);
    }
    assert(mesh_calls==1920 && ring_calls>0 && sprite_calls>mesh_calls);
    assert(syncs>=starts);
    /* Title height changes only the top, never the other three TV edges. */
    expected_ring_color=0;
    assert(md_start());
    expected_left=26; expected_top=84; expected_width=508; expected_height=210;
    md_set_tv_title_bottom(79);
    assert(md_frame(1,0,bands,0,test_time,0));
    expected_top=100; expected_height=194;
    md_set_tv_title_bottom(95);
    assert(md_frame(1,0,bands,0,test_time,0));
    md_stop();
    md_set_tv_title_bottom(81);
    expected_ring_color=0;
    expected_left=38; expected_top=74; expected_width=306; expected_height=75;
    assert(md_start());
    render_cost=50000;
    assert(md_frame(0,0,bands,0,test_time,0));
    assert(md_next==test_time+150000);
    {
        int calls=starts;
        test_time+=100000;
        assert(md_frame(0,0,bands,0,test_time,0) && starts==calls);
    }
    md_stop();
    assert(md_start());
    render_cost=0;
    assert(md_frame(0,0,bands,0,test_time,0));
    {
        int calls=starts;
        expected_left=expected_top=0; expected_width=480; expected_height=272;
        assert(md_frame(0,1,bands,0,test_time,0) && starts==calls+1);
        expected_left=38; expected_top=74; expected_width=306; expected_height=75;
        assert(md_frame(0,0,bands,0,test_time,0) && starts==calls+2);
    }
    md_stop();
    for(int i=1998848;i<edram_size;i++) assert(vram[i]==0xa5);
    munmap(vram,edram_size);
    return 0;
}
